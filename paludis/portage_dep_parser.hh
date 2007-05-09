/* vim: set sw=4 sts=4 et foldmethod=syntax : */

/*
 * Copyright (c) 2005, 2006, 2007 Ciaran McCreesh <ciaranm@ciaranm.org>
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

#ifndef PALUDIS_GUARD_PALUDIS_PORTAGE_DEP_PARSER_HH
#define PALUDIS_GUARD_PALUDIS_PORTAGE_DEP_PARSER_HH 1

#include <paludis/dep_spec.hh>
#include <paludis/portage_dep_lexer.hh>
#include <paludis/util/exception.hh>
#include <paludis/util/instantiation_policy.hh>
#include <string>

/** \file
 * Declarations for the PortageDepParser class.
 *
 * \ingroup grpdepparser
 */

namespace paludis
{
    /**
     * A DepStringParseError is thrown if an error is encountered when parsing
     * a dependency string.
     *
     * \ingroup grpexceptions
     * \ingroup grpdepparser
     */
    class PALUDIS_VISIBLE DepStringParseError : public DepStringError
    {
        public:
            /**
             * Constructor.
             */
            DepStringParseError(const std::string & dep_string,
                    const std::string & message) throw ();
    };

    /**
     * A DepStringNestingError is thrown if a dependency string does not have
     * properly balanced parentheses.
     *
     * \ingroup grpexceptions
     * \ingroup grpdepparser
     */
    class PALUDIS_VISIBLE DepStringNestingError : public DepStringParseError
    {
        public:
            /**
             * Constructor.
             */
            DepStringNestingError(const std::string & dep_string) throw ();
    };

    /**
     * Interface provided by PortageDepParserPolicy classes.
     *
     * \see PortageDepParserPolicy
     *
     * \ingroup grpdepparser
     */
    struct PALUDIS_VISIBLE PortageDepParserPolicyInterface
    {
        /**
         * Create a new text spec from the provided string.
         */
        virtual std::tr1::shared_ptr<DepSpec> new_text_spec(const std::string &) const = 0;

        /**
         * Are || ( ) deps permitted?
         */
        virtual bool permit_any_deps() const = 0;

        /**
         * Destructor.
         */
        virtual ~PortageDepParserPolicyInterface()
        {
        }
    };

    /**
     * The PortageDepParser converts string representations of a dependency
     * specification into a DepSpec instance. The PortageDepLexer class is
     * used as the first stage.
     *
     * \ingroup grpdepparser
     */
    class PALUDIS_VISIBLE PortageDepParser :
        private InstantiationPolicy<PortageDepParser, instantiation_method::NonInstantiableTag>
    {
        public:
            /**
             * Controls how a PortageDepParser treats any and text groups.
             *
             * \see PortageDepParser
             * \ingroup grpdepparser
             */
            class PALUDIS_VISIBLE Policy
            {
                private:
                    const bool _permit_any_deps;
                    const PackageDepSpecParseMode _parse_mode;
                    std::tr1::shared_ptr<StringDepSpec> (Policy::* const _create_func) (const std::string &) const;

                    std::tr1::shared_ptr<StringDepSpec> _create_text_dep_spec(const std::string &) const;
                    std::tr1::shared_ptr<StringDepSpec> _create_package_dep_spec(const std::string &) const;

                    Policy(const bool, const PackageDepSpecParseMode,
                            std::tr1::shared_ptr<StringDepSpec> (Policy::* const) (const std::string &) const);

                public:
                    /**
                     * Return a Policy object that makes text specs
                     * PlainTextDepSpec instances.
                     */
                    static Policy text_is_text_dep_spec(bool permit_any_deps);

                    /**
                     * Return a Policy object that makes text specs
                     * PackageDepSpec instances.
                     */
                    static Policy text_is_package_dep_spec(bool permit_any_deps, PackageDepSpecParseMode);

                    /**
                     * Create a text dep spec.
                     */
                    std::tr1::shared_ptr<StringDepSpec> create(const std::string &) const;

                    /**
                     * Whether any deps are permitted.
                     */
                    bool permit_any_deps() const;
            };

            /**
             * Parse a given dependency string, and return an appropriate
             * DepSpec tree.
             */
            static std::tr1::shared_ptr<CompositeDepSpec> parse(const std::string & s,
                    const Policy &);

            /**
             * Convenience wrapper for parse for depend strings, for VersionMetadata.
             */
            static std::tr1::shared_ptr<const CompositeDepSpec> parse_depend(const std::string & s,
                    const PackageDepSpecParseMode);

            /**
             * Convenience wrapper for parse for license strings, for VersionMetadata.
             */
            static std::tr1::shared_ptr<const CompositeDepSpec> parse_license(const std::string & s);
    };
}

#endif
