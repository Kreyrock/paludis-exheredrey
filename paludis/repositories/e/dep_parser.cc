/* vim: set sw=4 sts=4 et foldmethod=syntax : */

/*
 * Copyright (c) 2005, 2006, 2007, 2008 Ciaran McCreesh
 *
 * This file is part of the Paludis package manager. Paludis is free software;
 * you can redistribute it and/or modify it under the terms of the GNU General
 * Public License version 2, as published by the Free Software Foundation.
 *
 * Paludis is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <paludis/repositories/e/dep_parser.hh>
#include <paludis/repositories/e/eapi.hh>
#include <paludis/util/stringify.hh>
#include <paludis/util/make_shared_ptr.hh>
#include <paludis/util/options.hh>
#include <paludis/util/visitor-impl.hh>
#include <paludis/util/tokeniser.hh>
#include <paludis/util/make_named_values.hh>
#include <paludis/util/map.hh>
#include <paludis/util/wrapped_forward_iterator.hh>
#include <paludis/elike_dep_parser.hh>
#include <paludis/elike_conditional_dep_spec.hh>
#include <paludis/elike_package_dep_spec.hh>
#include <paludis/dep_spec.hh>
#include <paludis/environment.hh>
#include <paludis/repository.hh>
#include <paludis/package_id.hh>
#include <paludis/metadata_key.hh>
#include <paludis/literal_metadata_key.hh>
#include <list>
#include <set>
#include <ostream>

using namespace paludis;
using namespace paludis::erepository;

#include <paludis/repositories/e/dep_parser-se.cc>

EDepParseError::EDepParseError(const std::string & s, const std::string & t) throw () :
    Exception("Error parsing '" + s + "': " + t)
{
}

namespace paludis
{
    namespace n
    {
        struct add_handler;
        struct item;
        struct spec;
    }
}

namespace
{
    template <typename T_>
    struct ParseStackTypes
    {
        typedef std::tr1::function<void (const std::tr1::shared_ptr<const typename T_::ConstItem> &)> AddHandler;

        struct Item
        {
            NamedValue<n::add_handler, AddHandler> add_handler;
            NamedValue<n::item, const std::tr1::shared_ptr<const typename T_::ConstItem> > item;
            NamedValue<n::spec, std::tr1::shared_ptr<DepSpec> > spec;
        };

        typedef std::list<Item> Stack;
        typedef std::tr1::function<void (const std::tr1::shared_ptr<DepSpec> &)> AnnotationsGoHere;
    };

    template <typename T_>
    void package_dep_spec_string_handler(
            const typename ParseStackTypes<T_>::Stack & h,
            const typename ParseStackTypes<T_>::AnnotationsGoHere & annotations_go_here,
            const std::string & s,
            const EAPI & eapi,
            const std::tr1::shared_ptr<const PackageID> & id)
    {
        std::tr1::shared_ptr<PackageDepSpec> spec(new PackageDepSpec(
                    parse_elike_package_dep_spec(s, eapi.supported()->package_dep_spec_parse_options(), id)));
        (*h.begin()).add_handler()(make_shared_ptr(new TreeLeaf<T_, PackageDepSpec>(spec)));
        annotations_go_here(spec);
    }

    template <typename T_>
    void package_or_block_dep_spec_string_handler(
            const typename ParseStackTypes<T_>::Stack & h,
            const typename ParseStackTypes<T_>::AnnotationsGoHere & annotations_go_here,
            const std::string & s,
            const EAPI & eapi,
            const std::tr1::shared_ptr<const PackageID> & id)
    {
        if ((! s.empty()) && ('!' == s.at(0)))
        {
            std::string::size_type specstart(1);
            if (2 <= s.length() && '!' == s.at(1))
            {
                if (! eapi.supported()->dependency_spec_tree_parse_options()[dstpo_hard_soft_blocks])
                    throw EDepParseError(s, "Double-! blocks not allowed in this EAPI");
                specstart = 2;
            }

            std::tr1::shared_ptr<BlockDepSpec> spec(new BlockDepSpec(
                        make_shared_ptr(new PackageDepSpec(parse_elike_package_dep_spec(s.substr(specstart),
                                    eapi.supported()->package_dep_spec_parse_options(), id))), s));
            (*h.begin()).add_handler()(make_shared_ptr(new TreeLeaf<T_, BlockDepSpec>(spec)));
            annotations_go_here(spec);
        }
        else
            package_dep_spec_string_handler<T_>(h, annotations_go_here, s, eapi, id);
    }

    template <typename T_>
    void license_handler(
            const typename ParseStackTypes<T_>::Stack & h,
            const typename ParseStackTypes<T_>::AnnotationsGoHere & annotations_go_here,
            const std::string & s)
    {
        std::tr1::shared_ptr<LicenseDepSpec> spec(new LicenseDepSpec(s));
        (*h.begin()).add_handler()(make_shared_ptr(new TreeLeaf<T_, LicenseDepSpec>(spec)));
        annotations_go_here(spec);
    }

    template <typename T_>
    void plain_text_handler(
            const typename ParseStackTypes<T_>::Stack & h,
            const typename ParseStackTypes<T_>::AnnotationsGoHere & annotations_go_here,
            const std::string & s)
    {
        std::tr1::shared_ptr<PlainTextDepSpec> spec(new PlainTextDepSpec(s));
        (*h.begin()).add_handler()(make_shared_ptr(new TreeLeaf<T_, PlainTextDepSpec>(spec)));
        annotations_go_here(spec);
    }

    template <typename T_>
    void simple_uri_handler(
            const typename ParseStackTypes<T_>::Stack & h,
            const typename ParseStackTypes<T_>::AnnotationsGoHere & annotations_go_here,
            const std::string & s)
    {
        std::tr1::shared_ptr<SimpleURIDepSpec> spec(new SimpleURIDepSpec(s));
        (*h.begin()).add_handler()(make_shared_ptr(new TreeLeaf<T_, SimpleURIDepSpec>(spec)));
        annotations_go_here(spec);
    }

    template <typename T_>
    void arrow_handler(
            const typename ParseStackTypes<T_>::Stack & h,
            const typename ParseStackTypes<T_>::AnnotationsGoHere & annotations_go_here,
            const std::string & s,
            const std::string & f,
            const std::string & t,
            const EAPI & eapi)
    {
        if (t.empty() || eapi.supported()->dependency_spec_tree_parse_options()[dstpo_uri_supports_arrow])
        {
            std::tr1::shared_ptr<FetchableURIDepSpec> spec(new FetchableURIDepSpec(t.empty() ? f : f + " -> " + t));
            (*h.begin()).add_handler()(make_shared_ptr(new TreeLeaf<T_, FetchableURIDepSpec>(spec)));
            annotations_go_here(spec);
        }
        else
            throw EDepParseError(s, "Arrows not allowed in this EAPI");
    }

    void any_not_allowed_handler(const std::string & s) PALUDIS_ATTRIBUTE((noreturn));

    void any_not_allowed_handler(const std::string & s)
    {
        throw EDepParseError(s, "Any dep specs not allowed here");
    }

    void arrows_not_allowed_handler(const std::string & s, const std::string & f, const std::string & t) PALUDIS_ATTRIBUTE((noreturn));

    void arrows_not_allowed_handler(const std::string & s, const std::string & f, const std::string & t)
    {
        throw EDepParseError(s, "Arrow '" + f + " -> " + t + "' not allowed here");
    }

    void error_handler(const std::string & s, const std::string & t) PALUDIS_ATTRIBUTE((noreturn));

    void error_handler(const std::string & s, const std::string & t)
    {
        throw EDepParseError(s, t);
    }

    void labels_not_allowed_handler(const std::string & s, const std::string & f) PALUDIS_ATTRIBUTE((noreturn));

    void labels_not_allowed_handler(const std::string & s, const std::string & f)
    {
        throw EDepParseError(s, "Label '" + f + "' not allowed here");
    }

    template <typename T_>
    void dependency_label_handler(
            const typename ParseStackTypes<T_>::Stack & h,
            const typename ParseStackTypes<T_>::AnnotationsGoHere & annotations_go_here,
            const std::string & s,
            const EAPI & eapi)
    {
        std::tr1::shared_ptr<DependencyLabelsDepSpec> spec(parse_dependency_label(s, eapi));
        (*h.begin()).add_handler()(make_shared_ptr(new TreeLeaf<T_, DependencyLabelsDepSpec>(spec)));
        annotations_go_here(spec);
    }

    template <typename T_>
    void fetchable_label_handler(
            const typename ParseStackTypes<T_>::Stack & h,
            const typename ParseStackTypes<T_>::AnnotationsGoHere & annotations_go_here,
            const std::string & s,
            const EAPI & eapi)
    {
        std::tr1::shared_ptr<URILabelsDepSpec> spec(parse_uri_label(s, eapi));
        (*h.begin()).add_handler()(make_shared_ptr(new TreeLeaf<T_, URILabelsDepSpec>(spec)));
        annotations_go_here(spec);
    }

    template <typename T_, typename A_>
    void any_all_handler(typename ParseStackTypes<T_>::Stack & stack)
    {
        using namespace std::tr1::placeholders;
        std::tr1::shared_ptr<A_> spec(new A_);
        std::tr1::shared_ptr<ConstTreeSequence<T_, A_> > item(new ConstTreeSequence<T_, A_>(spec));
        (*stack.begin()).add_handler()(item);
        stack.push_front(make_named_values<typename ParseStackTypes<T_>::Item>(
                value_for<n::add_handler>(std::tr1::bind(&ConstTreeSequence<T_, A_>::add, item.get(), _1)),
                value_for<n::item>(item),
                value_for<n::spec>(spec)
                ));
    }

    template <typename T_>
    void use_handler(
            typename ParseStackTypes<T_>::Stack & stack,
            const std::string & u,
            const Environment * const env,
            const std::tr1::shared_ptr<const PackageID> & id)
    {
        using namespace std::tr1::placeholders;
        std::tr1::shared_ptr<ConditionalDepSpec> spec(new ConditionalDepSpec(parse_elike_conditional_dep_spec(u, env, id)));
        std::tr1::shared_ptr<ConstTreeSequence<T_, ConditionalDepSpec> > item(new ConstTreeSequence<T_, ConditionalDepSpec>(spec));
        (*stack.begin()).add_handler()(item);
        stack.push_front(make_named_values<typename ParseStackTypes<T_>::Item>(
                value_for<n::add_handler>(std::tr1::bind(&ConstTreeSequence<T_, ConditionalDepSpec>::add, item.get(), _1)),
                value_for<n::item>(item),
                value_for<n::spec>(spec)
                ));
    }

    template <typename T_>
    void pop_handler(
            typename ParseStackTypes<T_>::Stack & stack,
            const typename ParseStackTypes<T_>::AnnotationsGoHere & annotations_go_here,
            const std::string & s)
    {
        annotations_go_here(stack.begin()->spec());
        stack.pop_front();
        if (stack.empty())
            throw EDepParseError(s, "Too many ')'s");
    }

    template <typename T_>
    void should_be_empty_handler(const typename ParseStackTypes<T_>::Stack & stack, const std::string & s)
    {
        if (1 != stack.size())
            throw EDepParseError(s, "Nesting error");
    }

    void use_under_any_handler(const std::string & s, const EAPI & eapi)
    {
        if (eapi.supported()->dependency_spec_tree_parse_options()[dstpo_disallow_any_use])
            throw EDepParseError(s, "use? not allowed under || ( ) in this EAPI");
    }

    void do_nothing()
    {
    }

    struct AnnotationsKey :
        MetadataSectionKey
    {
        AnnotationsKey(const std::tr1::shared_ptr<const Map<std::string, std::string> > & m) :
            MetadataSectionKey("Annotations", "Annotations", mkt_normal)
        {
            for (Map<std::string, std::string>::ConstIterator k(m->begin()), k_end(m->end()) ;
                    k != k_end ; ++k)
                add_metadata_key(make_shared_ptr(new LiteralMetadataValueKey<std::string>(k->first, k->first, mkt_normal, k->second)));
        }

        void need_keys_added() const
        {
        }
    };

    void set_annotations(std::tr1::shared_ptr<DepSpec> & spec, const std::tr1::shared_ptr<const Map<std::string, std::string> > & m)
    {
        std::tr1::shared_ptr<AnnotationsKey> key(new AnnotationsKey(m));
        spec->set_annotations_key(key);
    }

    void set_thing_to_annotate(std::tr1::shared_ptr<DepSpec> & spec, const std::tr1::shared_ptr<DepSpec> & s)
    {
        spec = s;
    }
}

std::tr1::shared_ptr<DependencySpecTree::ConstItem>
paludis::erepository::parse_depend(const std::string & s,
        const Environment * const env, const std::tr1::shared_ptr<const PackageID> & id, const EAPI & eapi)
{
    using namespace std::tr1::placeholders;

    ParseStackTypes<DependencySpecTree>::Stack stack;
    std::tr1::shared_ptr<AllDepSpec> spec(new AllDepSpec);
    std::tr1::shared_ptr<DepSpec> thing_to_annotate(spec);
    std::tr1::shared_ptr<ConstTreeSequence<DependencySpecTree, AllDepSpec> > top(
            new ConstTreeSequence<DependencySpecTree, AllDepSpec>(spec));
    stack.push_front(make_named_values<ParseStackTypes<DependencySpecTree>::Item>(
                value_for<n::add_handler>(std::tr1::bind(&ConstTreeSequence<DependencySpecTree, AllDepSpec>::add, top.get(), _1)),
                value_for<n::item>(top),
                value_for<n::spec>(spec)
            ));

    ELikeDepParserCallbacks callbacks(
            make_named_values<ELikeDepParserCallbacks>(
                value_for<n::on_all>(std::tr1::bind(&any_all_handler<DependencySpecTree, AllDepSpec>, std::tr1::ref(stack))),
                value_for<n::on_annotations>(std::tr1::bind(&set_annotations, std::tr1::ref(thing_to_annotate), _1)),
                value_for<n::on_any>(std::tr1::bind(&any_all_handler<DependencySpecTree, AnyDepSpec>, std::tr1::ref(stack))),
                value_for<n::on_arrow>(std::tr1::bind(&arrows_not_allowed_handler, s, _1, _2)),
                value_for<n::on_error>(std::tr1::bind(&error_handler, s, _1)),
                value_for<n::on_label>(std::tr1::bind(&dependency_label_handler<DependencySpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<DependencySpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), _1, eapi)),
                value_for<n::on_pop>(std::tr1::bind(&pop_handler<DependencySpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<DependencySpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), s)),
                value_for<n::on_should_be_empty>(std::tr1::bind(&should_be_empty_handler<DependencySpecTree>, std::tr1::ref(stack), s)),
                value_for<n::on_string>(std::tr1::bind(&package_or_block_dep_spec_string_handler<DependencySpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<DependencySpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), _1, eapi, id)),
                value_for<n::on_use>(std::tr1::bind(&use_handler<DependencySpecTree>, std::tr1::ref(stack), _1, env, id)),
                value_for<n::on_use_under_any>(std::tr1::bind(&use_under_any_handler, s, eapi))
            ));

    parse_elike_dependencies(s, callbacks);

    return (*stack.begin()).item();
}

std::tr1::shared_ptr<ProvideSpecTree::ConstItem>
paludis::erepository::parse_provide(const std::string & s,
        const Environment * const env, const std::tr1::shared_ptr<const PackageID> & id, const EAPI & eapi)
{
    using namespace std::tr1::placeholders;

    ParseStackTypes<ProvideSpecTree>::Stack stack;
    std::tr1::shared_ptr<AllDepSpec> spec(new AllDepSpec);
    std::tr1::shared_ptr<DepSpec> thing_to_annotate(spec);
    std::tr1::shared_ptr<ConstTreeSequence<ProvideSpecTree, AllDepSpec> > top(
            new ConstTreeSequence<ProvideSpecTree, AllDepSpec>(spec));
    stack.push_front(make_named_values<ParseStackTypes<ProvideSpecTree>::Item>(
                value_for<n::add_handler>(std::tr1::bind(&ConstTreeSequence<ProvideSpecTree, AllDepSpec>::add, top.get(), _1)),
                value_for<n::item>(top),
                value_for<n::spec>(spec)
            ));

    ELikeDepParserCallbacks callbacks(
            make_named_values<ELikeDepParserCallbacks>(
                value_for<n::on_all>(std::tr1::bind(&any_all_handler<ProvideSpecTree, AllDepSpec>, std::tr1::ref(stack))),
                value_for<n::on_annotations>(std::tr1::bind(&set_annotations, std::tr1::ref(thing_to_annotate), _1)),
                value_for<n::on_any>(std::tr1::bind(&any_not_allowed_handler, s)),
                value_for<n::on_arrow>(std::tr1::bind(&arrows_not_allowed_handler, s, _1, _2)),
                value_for<n::on_error>(std::tr1::bind(&error_handler, s, _1)),
                value_for<n::on_label>(std::tr1::bind(&labels_not_allowed_handler, s, _1)),
                value_for<n::on_pop>(std::tr1::bind(&pop_handler<ProvideSpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<ProvideSpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), s)),
                value_for<n::on_should_be_empty>(std::tr1::bind(&should_be_empty_handler<ProvideSpecTree>, std::tr1::ref(stack), s)),
                value_for<n::on_string>(std::tr1::bind(&package_dep_spec_string_handler<ProvideSpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<DependencySpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), _1, eapi, id)),
                value_for<n::on_use>(std::tr1::bind(&use_handler<ProvideSpecTree>, std::tr1::ref(stack), _1, env, id)),
                value_for<n::on_use_under_any>(std::tr1::bind(&use_under_any_handler, s, eapi))
            ));

    parse_elike_dependencies(s, callbacks);

    return (*stack.begin()).item();
}

std::tr1::shared_ptr<FetchableURISpecTree::ConstItem>
paludis::erepository::parse_fetchable_uri(const std::string & s,
        const Environment * const env, const std::tr1::shared_ptr<const PackageID> & id, const EAPI & eapi)
{
    using namespace std::tr1::placeholders;

    ParseStackTypes<FetchableURISpecTree>::Stack stack;
    std::tr1::shared_ptr<AllDepSpec> spec(new AllDepSpec);
    std::tr1::shared_ptr<DepSpec> thing_to_annotate(spec);
    std::tr1::shared_ptr<ConstTreeSequence<FetchableURISpecTree, AllDepSpec> > top(
            new ConstTreeSequence<FetchableURISpecTree, AllDepSpec>(spec));
    stack.push_front(make_named_values<ParseStackTypes<FetchableURISpecTree>::Item>(
                value_for<n::add_handler>(std::tr1::bind(&ConstTreeSequence<FetchableURISpecTree, AllDepSpec>::add, top.get(), _1)),
                value_for<n::item>(top),
                value_for<n::spec>(spec)
                ));

    ELikeDepParserCallbacks callbacks(
            make_named_values<ELikeDepParserCallbacks>(
                value_for<n::on_all>(std::tr1::bind(&any_all_handler<FetchableURISpecTree, AllDepSpec>, std::tr1::ref(stack))),
                value_for<n::on_annotations>(std::tr1::bind(&set_annotations, std::tr1::ref(thing_to_annotate), _1)),
                value_for<n::on_any>(std::tr1::bind(&any_not_allowed_handler, s)),
                value_for<n::on_arrow>(std::tr1::bind(&arrow_handler<FetchableURISpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<FetchableURISpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), s, _1, _2, eapi)),
                value_for<n::on_error>(std::tr1::bind(&error_handler, s, _1)),
                value_for<n::on_label>(std::tr1::bind(&fetchable_label_handler<FetchableURISpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<FetchableURISpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), _1, eapi)),
                value_for<n::on_pop>(std::tr1::bind(&pop_handler<FetchableURISpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<FetchableURISpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), s)),
                value_for<n::on_should_be_empty>(std::tr1::bind(&should_be_empty_handler<FetchableURISpecTree>, std::tr1::ref(stack), s)),
                value_for<n::on_string>(std::tr1::bind(&arrow_handler<FetchableURISpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<FetchableURISpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), s, _1, "", eapi)),
                value_for<n::on_use>(std::tr1::bind(&use_handler<FetchableURISpecTree>, std::tr1::ref(stack), _1, env, id)),
                value_for<n::on_use_under_any>(std::tr1::bind(&use_under_any_handler, s, eapi))
            ));

    parse_elike_dependencies(s, callbacks);

    return (*stack.begin()).item();
}

std::tr1::shared_ptr<SimpleURISpecTree::ConstItem>
paludis::erepository::parse_simple_uri(const std::string & s,
        const Environment * const env, const std::tr1::shared_ptr<const PackageID> & id, const EAPI &)
{
    using namespace std::tr1::placeholders;

    ParseStackTypes<SimpleURISpecTree>::Stack stack;
    std::tr1::shared_ptr<AllDepSpec> spec(new AllDepSpec);
    std::tr1::shared_ptr<DepSpec> thing_to_annotate(spec);
    std::tr1::shared_ptr<ConstTreeSequence<SimpleURISpecTree, AllDepSpec> > top(
            new ConstTreeSequence<SimpleURISpecTree, AllDepSpec>(spec));
    stack.push_front(make_named_values<ParseStackTypes<SimpleURISpecTree>::Item>(
                value_for<n::add_handler>(std::tr1::bind(&ConstTreeSequence<SimpleURISpecTree, AllDepSpec>::add, top.get(), _1)),
                value_for<n::item>(top),
                value_for<n::spec>(spec)
            ));

    ELikeDepParserCallbacks callbacks(
            make_named_values<ELikeDepParserCallbacks>(
                value_for<n::on_all>(std::tr1::bind(&any_all_handler<SimpleURISpecTree, AllDepSpec>, std::tr1::ref(stack))),
                value_for<n::on_annotations>(std::tr1::bind(&set_annotations, std::tr1::ref(thing_to_annotate), _1)),
                value_for<n::on_any>(std::tr1::bind(&any_not_allowed_handler, s)),
                value_for<n::on_arrow>(std::tr1::bind(&arrows_not_allowed_handler, s, _1, _2)),
                value_for<n::on_error>(std::tr1::bind(&error_handler, s, _1)),
                value_for<n::on_label>(std::tr1::bind(&labels_not_allowed_handler, s, _1)),
                value_for<n::on_pop>(std::tr1::bind(&pop_handler<SimpleURISpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<DependencySpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), s)),
                value_for<n::on_should_be_empty>(std::tr1::bind(&should_be_empty_handler<SimpleURISpecTree>, std::tr1::ref(stack), s)),
                value_for<n::on_string>(std::tr1::bind(&simple_uri_handler<SimpleURISpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<SimpleURISpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), _1)),
                value_for<n::on_use>(std::tr1::bind(&use_handler<SimpleURISpecTree>, std::tr1::ref(stack), _1, env, id)),
                value_for<n::on_use_under_any>(&do_nothing)
            ));

    parse_elike_dependencies(s, callbacks);

    return (*stack.begin()).item();
}

std::tr1::shared_ptr<LicenseSpecTree::ConstItem>
paludis::erepository::parse_license(const std::string & s,
        const Environment * const env, const std::tr1::shared_ptr<const PackageID> & id, const EAPI & eapi)
{
    using namespace std::tr1::placeholders;

    ParseStackTypes<LicenseSpecTree>::Stack stack;
    std::tr1::shared_ptr<AllDepSpec> spec(new AllDepSpec);
    std::tr1::shared_ptr<DepSpec> thing_to_annotate(spec);
    std::tr1::shared_ptr<ConstTreeSequence<LicenseSpecTree, AllDepSpec> > top(
            new ConstTreeSequence<LicenseSpecTree, AllDepSpec>(spec));
    stack.push_front(make_named_values<ParseStackTypes<LicenseSpecTree>::Item>(
                value_for<n::add_handler>(std::tr1::bind(&ConstTreeSequence<LicenseSpecTree, AllDepSpec>::add, top.get(), _1)),
                value_for<n::item>(top),
                value_for<n::spec>(spec)
            ));

    ELikeDepParserCallbacks callbacks(
            make_named_values<ELikeDepParserCallbacks>(
                value_for<n::on_all>(std::tr1::bind(&any_all_handler<LicenseSpecTree, AllDepSpec>, std::tr1::ref(stack))),
                value_for<n::on_annotations>(std::tr1::bind(&set_annotations, std::tr1::ref(thing_to_annotate), _1)),
                value_for<n::on_any>(std::tr1::bind(&any_all_handler<LicenseSpecTree, AnyDepSpec>, std::tr1::ref(stack))),
                value_for<n::on_arrow>(std::tr1::bind(&arrows_not_allowed_handler, s, _1, _2)),
                value_for<n::on_error>(std::tr1::bind(&error_handler, s, _1)),
                value_for<n::on_label>(std::tr1::bind(&labels_not_allowed_handler, s, _1)),
                value_for<n::on_pop>(std::tr1::bind(&pop_handler<LicenseSpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<DependencySpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), s)),
                value_for<n::on_should_be_empty>(std::tr1::bind(&should_be_empty_handler<LicenseSpecTree>, std::tr1::ref(stack), s)),
                value_for<n::on_string>(std::tr1::bind(&license_handler<LicenseSpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<LicenseSpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), _1)),
                value_for<n::on_use>(std::tr1::bind(&use_handler<LicenseSpecTree>, std::tr1::ref(stack), _1, env, id)),
                value_for<n::on_use_under_any>(std::tr1::bind(&use_under_any_handler, s, eapi))
            ));

    parse_elike_dependencies(s, callbacks);

    return (*stack.begin()).item();
}

std::tr1::shared_ptr<PlainTextSpecTree::ConstItem>
paludis::erepository::parse_plain_text(const std::string & s,
        const Environment * const env, const std::tr1::shared_ptr<const PackageID> & id, const EAPI &)
{
    using namespace std::tr1::placeholders;

    ParseStackTypes<PlainTextSpecTree>::Stack stack;
    std::tr1::shared_ptr<AllDepSpec> spec(new AllDepSpec);
    std::tr1::shared_ptr<DepSpec> thing_to_annotate(spec);
    std::tr1::shared_ptr<ConstTreeSequence<PlainTextSpecTree, AllDepSpec> > top(
            new ConstTreeSequence<PlainTextSpecTree, AllDepSpec>(spec));
    stack.push_front(make_named_values<ParseStackTypes<PlainTextSpecTree>::Item>(
                value_for<n::add_handler>(std::tr1::bind(&ConstTreeSequence<PlainTextSpecTree, AllDepSpec>::add, top.get(), _1)),
                value_for<n::item>(top),
                value_for<n::spec>(spec)
            ));

    ELikeDepParserCallbacks callbacks(
            make_named_values<ELikeDepParserCallbacks>(
                value_for<n::on_all>(std::tr1::bind(&any_all_handler<PlainTextSpecTree, AllDepSpec>, std::tr1::ref(stack))),
                value_for<n::on_annotations>(std::tr1::bind(&set_annotations, std::tr1::ref(thing_to_annotate), _1)),
                value_for<n::on_any>(std::tr1::bind(&any_not_allowed_handler, s)),
                value_for<n::on_arrow>(std::tr1::bind(&arrows_not_allowed_handler, s, _1, _2)),
                value_for<n::on_error>(std::tr1::bind(&error_handler, s, _1)),
                value_for<n::on_label>(std::tr1::bind(&labels_not_allowed_handler, s, _1)),
                value_for<n::on_pop>(std::tr1::bind(&pop_handler<PlainTextSpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<PlainTextSpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), s)),
                value_for<n::on_should_be_empty>(std::tr1::bind(&should_be_empty_handler<PlainTextSpecTree>, std::tr1::ref(stack), s)),
                value_for<n::on_string>(std::tr1::bind(&plain_text_handler<PlainTextSpecTree>, std::tr1::ref(stack),
                        ParseStackTypes<PlainTextSpecTree>::AnnotationsGoHere(std::tr1::bind(
                                &set_thing_to_annotate, std::tr1::ref(thing_to_annotate), _1)), _1)),
                value_for<n::on_use>(std::tr1::bind(&use_handler<PlainTextSpecTree>, std::tr1::ref(stack), _1, env, id)),
                value_for<n::on_use_under_any>(&do_nothing)
            ));

    parse_elike_dependencies(s, callbacks);

    return (*stack.begin()).item();
}

std::tr1::shared_ptr<URILabelsDepSpec>
paludis::erepository::parse_uri_label(const std::string & s, const EAPI & e)
{
    Context context("When parsing label string '" + s + "' using EAPI '" + e.name() + "':");

    if (s.empty())
        throw EDepParseError(s, "Empty label");

    std::string c(e.supported()->uri_labels()->class_for_label(s.substr(0, s.length() - 1)));
    if (c.empty())
        throw EDepParseError(s, "Unknown label");

    std::tr1::shared_ptr<URILabelsDepSpec> l(new LabelsDepSpec<URILabelVisitorTypes>);

    if (c == "URIMirrorsThenListedLabel")
        l->add_label(make_shared_ptr(new URIMirrorsThenListedLabel(s.substr(0, s.length() - 1))));
    else if (c == "URIMirrorsOnlyLabel")
        l->add_label(make_shared_ptr(new URIMirrorsOnlyLabel(s.substr(0, s.length() - 1))));
    else if (c == "URIListedOnlyLabel")
        l->add_label(make_shared_ptr(new URIListedOnlyLabel(s.substr(0, s.length() - 1))));
    else if (c == "URIListedThenMirrorsLabel")
        l->add_label(make_shared_ptr(new URIListedThenMirrorsLabel(s.substr(0, s.length() - 1))));
    else if (c == "URILocalMirrorsOnlyLabel")
        l->add_label(make_shared_ptr(new URILocalMirrorsOnlyLabel(s.substr(0, s.length() - 1))));
    else if (c == "URIManualOnlyLabel")
        l->add_label(make_shared_ptr(new URIManualOnlyLabel(s.substr(0, s.length() - 1))));
    else
        throw EDepParseError(s, "Label '" + s + "' maps to unknown class '" + c + "'");

    return l;
}

std::tr1::shared_ptr<DependencyLabelsDepSpec>
paludis::erepository::parse_dependency_label(const std::string & s, const EAPI & e)
{
    Context context("When parsing label string '" + s + "' using EAPI '" + e.name() + "':");

    if (s.empty())
        throw EDepParseError(s, "Empty label");

    std::set<std::string> labels;
    std::string label(s.substr(0, s.length() - 1));
    tokenise<delim_kind::AnyOfTag, delim_mode::DelimiterTag>(label, ",+", "", std::inserter(labels, labels.end()));

    std::tr1::shared_ptr<DependencyLabelsDepSpec> l(new DependencyLabelsDepSpec);

    for (std::set<std::string>::iterator it = labels.begin(), it_e = labels.end(); it != it_e; ++it)
    {
        std::string c(e.supported()->dependency_labels()->class_for_label(*it));
        if (c.empty())
            throw EDepParseError(s, "Unknown label '" + *it + "'");

        if (c == "DependencyHostLabel")
            l->add_label(make_shared_ptr(new DependencyHostLabel(*it)));
        else if (c == "DependencyTargetLabel")
            l->add_label(make_shared_ptr(new DependencyTargetLabel(*it)));
        else if (c == "DependencyBuildLabel")
            l->add_label(make_shared_ptr(new DependencyBuildLabel(*it)));
        else if (c == "DependencyRunLabel")
            l->add_label(make_shared_ptr(new DependencyRunLabel(*it)));
        else if (c == "DependencyPostLabel")
            l->add_label(make_shared_ptr(new DependencyPostLabel(*it)));
        else if (c == "DependencyInstallLabel")
            l->add_label(make_shared_ptr(new DependencyInstallLabel(*it)));
        else if (c == "DependencyCompileLabel")
            l->add_label(make_shared_ptr(new DependencyCompileLabel(*it)));
        else if (c == "DependencySuggestedLabel")
            l->add_label(make_shared_ptr(new DependencySuggestedLabel(*it)));
        else if (c == "DependencyRecommendedLabel")
            l->add_label(make_shared_ptr(new DependencyRecommendedLabel(*it)));
        else if (c == "DependencyRequiredLabel")
            l->add_label(make_shared_ptr(new DependencyRequiredLabel(*it)));
        else if (c == "DependencyAnyLabel")
            l->add_label(make_shared_ptr(new DependencyAnyLabel(*it)));
        else if (c == "DependencyMineLabel")
            l->add_label(make_shared_ptr(new DependencyMineLabel(*it)));
        else if (c == "DependencyPrimaryLabel")
            l->add_label(make_shared_ptr(new DependencyPrimaryLabel(*it)));
        else if (c == "DependencyABILabel")
            l->add_label(make_shared_ptr(new DependencyABILabel(*it)));
        else
            throw EDepParseError(s, "Label '" + *it + "' maps to unknown class '" + c + "'");
    }

    return l;
}

