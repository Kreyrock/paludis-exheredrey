/* vim: set sw=4 sts=4 et foldmethod=syntax : */

/*
 * Copyright (c) 2007, 2008 Ciaran McCreesh
 * Copyright (c) 2008 Fernando J. Pereda
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

#include <paludis/merger.hh>
#include <paludis/util/dir_iterator.hh>
#include <paludis/util/stringify.hh>
#include <paludis/util/fd_holder.hh>
#include <paludis/util/log.hh>
#include <paludis/util/options.hh>
#include <paludis/util/hashes.hh>
#include <paludis/util/private_implementation_pattern-impl.hh>
#include <paludis/selinux/security_context.hh>
#include <paludis/environment.hh>
#include <paludis/hook.hh>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <list>
#include <tr1/unordered_map>

#include "config.h"
#ifdef HAVE_XATTRS
#  include <attr/xattr.h>
#endif

using namespace paludis;

#include <paludis/merger-se.cc>

typedef std::tr1::unordered_map<std::pair<dev_t, ino_t>, std::string, Hash<std::pair<dev_t, ino_t> > > MergedMap;

namespace paludis
{
    template <>
    struct Implementation<Merger>
    {
        MergedMap merged_ids;
        MergerParams params;
        bool result;
        bool skip_dir;

        Implementation(const MergerParams & p) :
            params(p),
            result(true),
            skip_dir(false)
        {
        }
    };
}

MergerError::MergerError(const std::string & s) throw () :
    Exception(s)
{
}

Merger::Merger(const MergerParams & p) :
    PrivateImplementationPattern<Merger>(new Implementation<Merger>(p))
{
}

Merger::~Merger()
{
}

bool
Merger::check()
{
    Context context("When checking merge from '" + stringify(_imp->params[k::image()]) + "' to '"
            + stringify(_imp->params[k::root()]) + "':");

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_check_pre")
                         ("INSTALL_SOURCE", stringify(_imp->params[k::image()]))
                         ("INSTALL_DESTINATION", stringify(_imp->params[k::root()])))).max_exit_status)
        make_check_fail();

    do_dir_recursive(true, _imp->params[k::image()], _imp->params[k::root()] / _imp->params[k::install_under()]);

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_check_post")
                         ("INSTALL_SOURCE", stringify(_imp->params[k::image()]))
                         ("INSTALL_DESTINATION", stringify(_imp->params[k::root()])))).max_exit_status)
        make_check_fail();

    return _imp->result;
}

void
Merger::make_check_fail()
{
    _imp->result = false;
}

void
Merger::merge()
{
    Context context("When performing merge from '" + stringify(_imp->params[k::image()]) + "' to '"
            + stringify(_imp->params[k::root()]) + "':");

    struct SaveUmask
    {
        mode_t m;

        SaveUmask(mode_t mm) :
            m(mm)
        {
        }

        ~SaveUmask()
        {
            ::umask(m);
        }
    } old_umask(::umask(0000));

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_install_pre")
                         ("INSTALL_SOURCE", stringify(_imp->params[k::image()]))
                         ("INSTALL_DESTINATION", stringify(_imp->params[k::root()])))).max_exit_status)
        Log::get_instance()->message("merger.pre_hooks.failure", ll_warning, lc_context) <<
            "Merge of '" << _imp->params[k::image()] << "' to '" << _imp->params[k::root()] << "' pre hooks returned non-zero";

    /* special handling for install_under */
    {
        Context local_context("When preparing install_under directory '" + stringify(_imp->params[k::install_under()]) + "' under root '"
                + stringify(_imp->params[k::root()]) + "':");

        std::list<FSEntry> dd;
        for (FSEntry d(_imp->params[k::root()].realpath() / _imp->params[k::install_under()]), d_end(_imp->params[k::root()].realpath()) ;
                d != d_end ; d = d.dirname())
            dd.push_front(d);
        for (std::list<FSEntry>::iterator d(dd.begin()), d_end(dd.end()) ; d != d_end ; ++d)
            if (! d->exists())
            {
                d->mkdir();
                record_install_under_dir(*d, MergeStatusFlags());
            }
            else
                record_install_under_dir(*d, MergeStatusFlags() + msi_used_existing);
    }

    do_dir_recursive(false, _imp->params[k::image()], (_imp->params[k::root()] / _imp->params[k::install_under()]).realpath());

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_install_post")
                         ("INSTALL_SOURCE", stringify(_imp->params[k::image()]))
                         ("INSTALL_DESTINATION", stringify(_imp->params[k::root()])))).max_exit_status)
        Log::get_instance()->message("merger.post_hooks.failure", ll_warning, lc_context) <<
            "Merge of '" << _imp->params[k::image()] << "' to '" << _imp->params[k::root()] << "' post hooks returned non-zero";
}

EntryType
Merger::entry_type(const FSEntry & f)
{
    Context context("When checking type of '" + stringify(f) + "':");

    if (! f.exists())
        return et_nothing;

    if (f.is_symbolic_link())
        return et_sym;

    if (f.is_regular_file())
        return et_file;

    if (f.is_directory())
        return et_dir;

    return et_misc;
}

void
Merger::do_dir_recursive(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    Context context("When " + stringify(is_check ? "checking" : "performing") + " merge from '" +
            stringify(src) + "' to '" + stringify(dst) + "':");

    if (! src.is_directory())
        throw MergerError("Source directory '" + stringify(src) + "' is not a directory");
    if ((! is_check) && (! dst.is_directory()))
        throw MergerError("Destination directory '" + stringify(dst) + "' is not a directory");

    on_enter_dir(is_check, src);

    DirIterator d(src, DirIteratorOptions() + dio_include_dotfiles + dio_inode_sort), d_end;

    if (is_check && d == d_end && dst != _imp->params[k::root()].realpath())
    {
        if (_imp->params[k::options()][mo_allow_empty_dirs])
            Log::get_instance()->message("merger.empty_directory", ll_warning, lc_context) << "Installing empty directory '"
                << stringify(dst) << "'";
        else
            on_error(is_check, "Attempted to install empty directory '" + stringify(dst) + "'");
    }

    for ( ; d != d_end ; ++d)
    {
        EntryType m(entry_type(*d));
        switch (m)
        {
            case et_sym:
                on_sym(is_check, *d, dst);
                continue;

            case et_file:
                on_file(is_check, *d, dst);
                continue;

            case et_dir:
                on_dir(is_check, *d, dst);
                if (_imp->result)
                {
                    if (! _imp->skip_dir)
                        do_dir_recursive(is_check, *d,
                                is_check ? (dst / d->basename()) : (dst / d->basename()).realpath());
                    else
                        _imp->skip_dir = false;
                }
                continue;

            case et_misc:
                on_misc(is_check, *d, dst);
                continue;

            case et_nothing:
            case last_et:
                ;
        }

        throw InternalError(PALUDIS_HERE, "Unexpected entry_type '" + stringify(m) + "'");
    }

    on_leave_dir(is_check, src);
}

void
Merger::on_file(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    Context context("When handling file '" + stringify(src) + "' to '" + stringify(dst) + "':");

    EntryType m(entry_type(dst / src.basename()));

    if (is_check &&
        0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_check_file_pre")
                         ("INSTALL_SOURCE", stringify(src))
                         ("INSTALL_DESTINATION", stringify(dst / src.basename())))).max_exit_status)
        make_check_fail();

    if (! is_check)
    {
        HookResult hr(_imp->params[k::environment()]->perform_hook(extend_hook(
                        Hook("merger_install_file_override")
                        ("INSTALL_SOURCE", stringify(src))
                        ("INSTALL_DESTINATION", stringify(dst / src.basename()))
                        .grab_output(Hook::AllowedOutputValues()("skip")))));

        if (hr.max_exit_status != 0)
            Log::get_instance()->message("merger.file.skip_hooks.failure", ll_warning, lc_context) << "Merge of '"
                << stringify(src) << "' to '" << stringify(dst) << "' skip hooks returned non-zero";
        else if (hr.output == "skip")
        {
            std::string tidy(stringify((dst / src.basename()).strip_leading(_imp->params[k::root()].realpath())));
            display_override("--- [skp] " + tidy);
            return;
        }
    }

    do
    {
        switch (m)
        {
            case et_nothing:
                on_file_over_nothing(is_check, src, dst);
                continue;

            case et_sym:
                on_file_over_sym(is_check, src, dst);
                continue;

            case et_dir:
                on_file_over_dir(is_check, src, dst);
                continue;

            case et_misc:
                on_file_over_misc(is_check, src, dst);
                continue;

            case et_file:
                on_file_over_file(is_check, src, dst);
                continue;

            case last_et:
                ;
        }

        throw InternalError(PALUDIS_HERE, "Unexpected entry_type '" + stringify(m) + "'");
    } while (false);

    if (is_check &&
        0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_check_file_post")
                         ("INSTALL_SOURCE", stringify(src))
                         ("INSTALL_DESTINATION", stringify(dst / src.basename())))).max_exit_status)
        make_check_fail();
}

void
Merger::on_dir(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    Context context("When handling dir '" + stringify(src) + "' to '" + stringify(dst) + "':");

    EntryType m(entry_type(dst / src.basename()));

    if (is_check &&
        0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_check_dir_pre")
                         ("INSTALL_SOURCE", stringify(src))
                         ("INSTALL_DESTINATION", stringify(dst / src.basename())))).max_exit_status)
        make_check_fail();

    if (! is_check)
    {
        HookResult hr(_imp->params[k::environment()]->perform_hook(extend_hook(
                        Hook("merger_install_dir_override")
                        ("INSTALL_SOURCE", stringify(src))
                        ("INSTALL_DESTINATION", stringify(dst / src.basename()))
                        .grab_output(Hook::AllowedOutputValues()("skip")))));

        if (hr.max_exit_status != 0)
            Log::get_instance()->message("merger.dir.skip_hooks.failure", ll_warning, lc_context) << "Merge of '"
                << stringify(src) << "' to '" << stringify(dst) << "' skip hooks returned non-zero";
        else if (hr.output == "skip")
        {
            std::string tidy(stringify((dst / src.basename()).strip_leading(_imp->params[k::root()].realpath())));
            display_override("--- [skp] " + tidy);
            _imp->skip_dir = true;
            return;
        }
    }

    do
    {
        switch (m)
        {
            case et_nothing:
                on_dir_over_nothing(is_check, src, dst);
                continue;

            case et_sym:
                on_dir_over_sym(is_check, src, dst);
                continue;

            case et_dir:
                on_dir_over_dir(is_check, src, dst);
                continue;

            case et_misc:
                on_dir_over_misc(is_check, src, dst);
                continue;

            case et_file:
                on_dir_over_file(is_check, src, dst);
                continue;

            case last_et:
                ;
        }

        throw InternalError(PALUDIS_HERE, "Unexpected entry_type '" + stringify(m) + "'");

    } while (false);

    if (is_check &&
        0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_check_dir_post")
                         ("INSTALL_SOURCE", stringify(src))
                         ("INSTALL_DESTINATION", stringify(dst / src.basename())))).max_exit_status)
        make_check_fail();
}

void
Merger::on_sym(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    Context context("When handling sym '" + stringify(src) + "' to '" + stringify(dst) + "':");

    EntryType m(entry_type(dst / src.basename()));

    if (is_check &&
        0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_check_sym_post")
                         ("INSTALL_SOURCE", stringify(src))
                         ("INSTALL_DESTINATION", stringify(dst / src.basename())))).max_exit_status)
        make_check_fail();

    if (! is_check)
    {
        HookResult hr(_imp->params[k::environment()]->perform_hook(extend_hook(
                        Hook("merger_install_sym_override")
                        ("INSTALL_SOURCE", stringify(src))
                        ("INSTALL_DESTINATION", stringify(dst / src.basename()))
                        .grab_output(Hook::AllowedOutputValues()("skip")))));

        if (hr.max_exit_status != 0)
            Log::get_instance()->message("merger.sym.skip_hooks.failure", ll_warning, lc_context) << "Merge of '"
                << stringify(src) << "' to '" << stringify(dst) << "' skip hooks returned non-zero";
        else if (hr.output == "skip")
        {
            std::string tidy(stringify((dst / src.basename()).strip_leading(_imp->params[k::root()].realpath())));
            display_override("--- [skp] " + tidy);
            return;
        }
    }
    else
    {
        if (symlink_needs_rewriting(src) && ! _imp->params[k::options()][mo_rewrite_symlinks])
            on_error(is_check, "Symlink to image detected at: " + stringify(src) + " (" + src.readlink() + ")");
    }

    do
    {
        switch (m)
        {
            case et_nothing:
                on_sym_over_nothing(is_check, src, dst);
                continue;

            case et_sym:
                on_sym_over_sym(is_check, src, dst);
                continue;

            case et_dir:
                on_sym_over_dir(is_check, src, dst);
                continue;

            case et_misc:
                on_sym_over_misc(is_check, src, dst);
                continue;

            case et_file:
                on_sym_over_file(is_check, src, dst);
                continue;

            case last_et:
                ;
        }

        throw InternalError(PALUDIS_HERE, "Unexpected entry_type '" + stringify(m) + "'");
    } while (false);

    if (is_check &&
        0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_check_sym_post")
                         ("INSTALL_SOURCE", stringify(src))
                         ("INSTALL_DESTINATION", stringify(dst / src.basename())))).max_exit_status)
        make_check_fail();
}

void
Merger::on_misc(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    Context context("When handling misc '" + stringify(src) + "' to '" + stringify(dst) + "':");

    on_error(is_check, "Cannot write '" + stringify(src) + "' to '" + stringify(dst) +
            "' because it is not a recognised file type");
}

void
Merger::on_enter_dir(bool, const FSEntry)
{
}

void
Merger::on_leave_dir(bool, const FSEntry)
{
}

void
Merger::on_file_over_nothing(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    if (is_check)
        return;

    record_install_file(src, dst, src.basename(), install_file(src, dst, src.basename()));
}

void
Merger::on_file_over_file(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    if (is_check)
        return;

    if (config_protected(src, dst))
    {
        std::string cfgpro_name(make_config_protect_name(src, dst));
        record_install_file(src, dst, cfgpro_name, install_file(src, dst, cfgpro_name));
    }
    else
    {
        unlink_file(dst / src.basename());
        record_install_file(src, dst, src.basename(), install_file(src, dst, src.basename()) + msi_unlinked_first);
    }
}

void
Merger::on_file_over_dir(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    on_error(is_check, "Cannot overwrite directory '" + stringify(dst / src.basename()) + "' with file '"
            + stringify(src) + "'");
}

void
Merger::on_file_over_sym(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    if (is_check)
        return;

    unlink_sym(dst / src.basename());
    record_install_file(src, dst, src.basename(), install_file(src, dst, src.basename()) + msi_unlinked_first);
}

void
Merger::on_file_over_misc(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    if (is_check)
        return;

    unlink_misc(dst / src.basename());
    record_install_file(src, dst, src.basename(), install_file(src, dst, src.basename()) + msi_unlinked_first);
}

void
Merger::on_dir_over_nothing(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    if (is_check)
        return;

    record_install_dir(src, dst, install_dir(src, dst));
}

void
Merger::on_dir_over_file(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    on_error(is_check, "Cannot overwrite file '" + stringify(dst / src.basename()) + "' with directory '"
            + stringify(src) + "'");
}

void
Merger::on_dir_over_dir(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    if (is_check)
        return;

    record_install_dir(src, dst, MergeStatusFlags() + msi_used_existing);
}

void
Merger::on_dir_over_sym(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    EntryType m;
    try
    {
        m = entry_type((dst / src.basename()).realpath());
    }
    catch (const FSError &)
    {
        m = et_nothing;
    }

    if (m == et_dir)
    {
        on_warn(is_check, "Expected '" + stringify(dst / src.basename()) +
                "' to be a directory but found a symlink to a directory");
        if (! is_check)
            record_install_dir(src, dst, MergeStatusFlags() + msi_used_existing);
    }
    else
        on_error(is_check, "Expected '" + stringify(dst / src.basename()) +
                "' to be a directory but found a symlink to a non-directory");
}

void
Merger::on_dir_over_misc(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    if (is_check)
        return;

    unlink_misc(dst / src.basename());
    record_install_dir(src, dst, install_dir(src, dst) + msi_unlinked_first);
}

void
Merger::on_sym_over_nothing(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    if (is_check)
        return;

    record_install_sym(src, dst, install_sym(src, dst));
}

void
Merger::on_sym_over_file(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    if (is_check)
        return;

    unlink_file(dst / src.basename());
    record_install_sym(src, dst, install_sym(src, dst) + msi_unlinked_first);
}

void
Merger::on_sym_over_dir(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    on_error(is_check, "Cannot overwrite directory '" + stringify(dst / src.basename()) + "' with symlink '"
            + stringify(src) + "'");
}

void
Merger::on_sym_over_sym(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    if (is_check)
        return;

    unlink_sym(dst / src.basename());
    record_install_sym(src, dst, install_sym(src, dst) + msi_unlinked_first);
}

void
Merger::on_sym_over_misc(bool is_check, const FSEntry & src, const FSEntry & dst)
{
    if (is_check)
        return;

    unlink_misc(dst / src.basename());
    record_install_sym(src, dst, install_sym(src, dst) + msi_unlinked_first);
}

MergeStatusFlags
Merger::install_file(const FSEntry & src, const FSEntry & dst_dir, const std::string & dst_name)
{
    Context context("When installing file '" + stringify(src) + "' to '" + stringify(dst_dir) + "' with protection '"
            + stringify(dst_name) + "':");
    MergeStatusFlags result;

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_install_file_pre")
                        ("INSTALL_SOURCE", stringify(src))
                        ("INSTALL_DESTINATION", stringify(dst_dir / src.basename())))).max_exit_status)
        Log::get_instance()->message("merger.file.pre_hooks.failure", ll_warning, lc_context) <<
                "Merge of '" << src << "' to '" << dst_dir << "' pre hooks returned non-zero";

    FSEntry dst(dst_dir / (stringify(dst_name) + "|paludis-midmerge"));
    FSEntry dst_real(dst_dir / dst_name);

    std::tr1::shared_ptr<const SecurityContext> secctx(MatchPathCon::get_instance()->match(stringify(dst_real), src.permissions()));
    FSCreateCon createcon(secctx);
    if (0 != paludis::setfilecon(src, secctx))
        throw MergerError("Could not set SELinux context on '"
                + stringify(src) + "': " + stringify(::strerror(errno)));

    mode_t src_perms(src.permissions());
    if (0 != (src_perms & (S_ISVTX | S_ISUID | S_ISGID)))
        result += msi_setid_bits;

    uid_t dest_uid(src.owner());
    gid_t dest_gid(src.group());

    if (! _imp->params[k::no_chown()])
    {
        uid_t new_uid(dest_uid == _imp->params[k::environment()]->reduced_uid() ? 0 : -1);
        gid_t new_gid(dest_gid == _imp->params[k::environment()]->reduced_gid() ? 0 : -1);
        if (uid_t(-1) != new_uid || gid_t(-1) != new_gid)
        {
            FSEntry(src).chown(new_uid, new_gid);
            result += msi_fixed_ownership;
        }
        dest_uid = new_uid == 0 ? 0 : dest_uid;
        dest_gid = new_gid == 0 ? 0 : dest_gid;
    }

    bool do_copy(false);

    if (0 == ::rename(stringify(src).c_str(), stringify(dst_real).c_str()))
    {
        result += msi_rename;
        if (! dst_real.utime())
            throw MergerError("utime(" + stringify(dst_real) + ", 0) failed: " + stringify(::strerror(errno)));

        /* set*id bits get partially clobbered on a rename on linux */
        dst_real.chmod(src_perms);

        _imp->merged_ids.insert(make_pair(src.lowlevel_id(), stringify(dst_real)));
    }
    else
    {
        do_copy = true;
        std::pair<MergedMap::const_iterator, MergedMap::const_iterator> ii(
                _imp->merged_ids.equal_range(src.lowlevel_id()));
        for (MergedMap::const_iterator i = ii.first ; i != ii.second ; ++i)
        {
            if (0 == ::link(i->second.c_str(), stringify(dst_real).c_str()))
            {
                do_copy = false;
                result += msi_as_hardlink;
                break;
            }
            Log::get_instance()->message("merger.file.link_failed", ll_debug, lc_context)
                    << "link(" << i->second << ", " << dst_real << ") failed: "
                    << ::strerror(errno);
        }
    }

    if (do_copy)
    {
        Log::get_instance()->message("merger.file.will_copy", ll_debug, lc_context) <<
            "rename/link failed: " << ::strerror(errno) << ". Falling back to regular read/write copy";
        FDHolder input_fd(::open(stringify(src).c_str(), O_RDONLY), false);
        if (-1 == input_fd)
            throw MergerError("Cannot read '" + stringify(src) + "': " + stringify(::strerror(errno)));

        FDHolder output_fd(::open(stringify(dst).c_str(), O_WRONLY | O_CREAT, src_perms), false);
        if (-1 == output_fd)
            throw MergerError("Cannot write '" + stringify(dst) + "': " + stringify(::strerror(errno)));

        if (! _imp->params[k::no_chown()])
            if (0 != ::fchown(output_fd, dest_uid, dest_gid))
                throw MergerError("Cannot fchown '" + stringify(dst) + "': " + stringify(::strerror(errno)));

        /* set*id bits */
        if (0 != ::fchmod(output_fd, src_perms))
            throw MergerError("Cannot fchmod '" + stringify(dst) + "': " + stringify(::strerror(errno)));
        try_to_copy_xattrs(src, output_fd, result);

        char buf[4096];
        ssize_t count;
        while ((count = read(input_fd, buf, 4096)) > 0)
            if (-1 == write(output_fd, buf, count))
                throw MergerError("write failed: " + stringify(::strerror(errno)));
        if (-1 == count)
            throw MergerError("read failed: " + stringify(::strerror(errno)));

        if (0 != ::rename(stringify(dst).c_str(), stringify(dst_real).c_str()))
            throw MergerError(
                    "rename(" + stringify(dst) + ", " + stringify(dst_real) + ") failed: " + stringify(::strerror(errno)));

        _imp->merged_ids.insert(make_pair(src.lowlevel_id(), stringify(dst_real)));
    }

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_install_file_post")
                         ("INSTALL_SOURCE", stringify(src))
                         ("INSTALL_DESTINATION", stringify(dst_dir / src.basename())))).max_exit_status)
        Log::get_instance()->message("merger.file.post_hooks.failed", ll_warning, lc_context) <<
            "Merge of '" << src << "' to '" << dst_dir << "' post hooks returned non-zero";

    return result;
}

bool
Merger::symlink_needs_rewriting(const FSEntry & sym)
{
    std::string target(sym.readlink());
    std::string real_image(stringify(_imp->params[k::image()].realpath()));

    return (0 == target.compare(0, real_image.length(), real_image));
}

void
Merger::rewrite_symlink_as_needed(const FSEntry & src, const FSEntry & dst_dir)
{
    if (! symlink_needs_rewriting(src))
        return;

    FSCreateCon createcon(MatchPathCon::get_instance()->match(stringify(dst_dir / src.basename()), S_IFLNK));

    FSEntry real_image(_imp->params[k::image()].realpath());
    FSEntry dst(src.readlink());
    std::string fixed_dst(stringify(dst.strip_leading(real_image)));

    Log::get_instance()->message("merger.rewriting_symlink", ll_qa, lc_context) << "Rewriting bad symlink: "
            << src << " -> " << dst << " to " << fixed_dst;

    FSEntry s(dst_dir / src.basename());
    s.unlink();
    s.symlink(fixed_dst);
}

void
Merger::record_renamed_dir_recursive(const FSEntry & dst)
{
    for (DirIterator d(dst, DirIteratorOptions() + dio_include_dotfiles + dio_inode_sort), d_end ; d != d_end ; ++d)
    {
        if (! _imp->params[k::no_chown()])
        {
            uid_t new_uid(d->owner() == _imp->params[k::environment()]->reduced_uid() ? 0 : -1);
            gid_t new_gid(d->group() == _imp->params[k::environment()]->reduced_gid() ? 0 : -1);
            if (uid_t(-1) != new_uid || gid_t(-1) != new_gid)
            {
                FSEntry f(*d);
                f.lchown(new_uid, new_gid);

                if (et_dir == entry_type(*d))
                {
                    mode_t mode(f.permissions());
                    if (uid_t(-1) != new_uid)
                        mode &= ~S_ISUID;
                    if (gid_t(-1) != new_gid)
                        mode &= ~S_ISGID;
                    f.chmod(mode);
                }
            }
        }

        EntryType m(entry_type(*d));
        switch (m)
        {
            case et_sym:
                rewrite_symlink_as_needed(*d, dst);
                record_install_sym(*d, dst, MergeStatusFlags() + msi_parent_rename);
                _imp->merged_ids.insert(make_pair(d->lowlevel_id(), stringify(*d)));
                continue;

            case et_file:
                record_install_file(*d, dst, stringify(d->basename()), MergeStatusFlags() + msi_parent_rename);
                _imp->merged_ids.insert(make_pair(d->lowlevel_id(), stringify(*d)));
                continue;

            case et_dir:
                record_install_dir(*d, d->dirname(), MergeStatusFlags() + msi_parent_rename);
                record_renamed_dir_recursive(*d);
                continue;

            case et_misc:
                throw MergerError("Unexpected 'et_misc' entry found at: " + stringify(*d));
                continue;

            case et_nothing:
            case last_et:
                ;
        }

        throw InternalError(PALUDIS_HERE, "Unexpected entry_type '" + stringify(m) + "'");
    }
}

void
Merger::relabel_dir_recursive(const FSEntry & src, const FSEntry & dst)
{
    for (DirIterator d(src, DirIteratorOptions() + dio_include_dotfiles + dio_inode_sort), d_end ; d != d_end ; ++d)
    {
        mode_t mode(d->permissions());
        std::tr1::shared_ptr<const SecurityContext> secctx(
                MatchPathCon::get_instance()->match(stringify(dst / d->basename()), mode));
        if (0 != paludis::setfilecon(*d, secctx))
            throw MergerError("Could not set SELinux context on '"
                    + stringify(*d) + "' : " + stringify(::strerror(errno)));
        if (d->is_directory())
            relabel_dir_recursive(*d, dst / d->basename());
    }
}

MergeStatusFlags
Merger::install_dir(const FSEntry & src, const FSEntry & dst_dir)
{
    Context context("When installing dir '" + stringify(src) + "' to '" + stringify(dst_dir) + "':");

    MergeStatusFlags result;

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_install_dir_pre")
                         ("INSTALL_SOURCE", stringify(src))
                         ("INSTALL_DESTINATION", stringify(dst_dir / src.basename())))).max_exit_status)
        Log::get_instance()->message("merger.dir.pre_hooks.failure", ll_warning, lc_context)
            << "Merge of '" << src << "' to '" << dst_dir << "' pre hooks returned non-zero";

    mode_t mode(src.permissions());
    uid_t dest_uid(src.owner());
    gid_t dest_gid(src.group());

    if (0 != (mode & (S_ISVTX | S_ISUID | S_ISGID)))
        result += msi_setid_bits;

    if (! _imp->params[k::no_chown()])
    {
        uid_t new_uid(dest_uid == _imp->params[k::environment()]->reduced_uid() ? 0 : -1);
        gid_t new_gid(dest_gid == _imp->params[k::environment()]->reduced_gid() ? 0 : -1);
        if (uid_t(-1) != new_uid)
            mode &= ~S_ISUID;
        if (gid_t(-1) != new_gid)
            mode &= ~S_ISGID;
        if (uid_t(-1) != new_uid || gid_t(-1) != new_gid)
        {
            result += msi_fixed_ownership;
            FSEntry f(src);
            f.chown(new_uid, new_gid);
            f.chmod(mode);
        }
        dest_uid = new_uid == 0 ? 0 : dest_uid;
        dest_gid = new_gid == 0 ? 0 : dest_gid;
    }

    FSEntry dst(dst_dir / src.basename());
    std::tr1::shared_ptr<const SecurityContext> secctx(MatchPathCon::get_instance()->match(stringify(dst), mode));
    FSCreateCon createcon(secctx);
    if (0 != paludis::setfilecon(src, secctx))
        throw MergerError("Could not set SELinux context on '"
                + stringify(src) + "': " + stringify(::strerror(errno)));

    if (is_selinux_enabled())
        relabel_dir_recursive(src, dst);

    if (0 == ::rename(stringify(src).c_str(), stringify(dst).c_str()))
    {
        result += msi_rename;
        record_renamed_dir_recursive(dst);
        _imp->skip_dir = true;
    }
    else
    {
        Log::get_instance()->message("merger.dir.rename_failed", ll_debug, lc_context) <<
            "rename failed. Falling back to recursive copy.";

        dst.mkdir(mode);
        FDHolder dst_fd(::open(stringify(dst).c_str(), O_RDONLY | O_DIRECTORY));
        if (-1 == dst_fd)
            throw MergerError("Could not get an FD for the directory '"
                    + stringify(dst) + "' that we just created: " + stringify(::strerror(errno)));
        if (! _imp->params[k::no_chown()])
            ::fchown(dst_fd, dest_uid, dest_gid);
        /* pick up set*id bits */
        ::fchmod(dst_fd, mode);
        try_to_copy_xattrs(src, dst_fd, result);
    }

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_install_dir_post")
                         ("INSTALL_SOURCE", stringify(src))
                         ("INSTALL_DESTINATION", stringify(dst_dir / src.basename())))).max_exit_status)
        Log::get_instance()->message("merger.dir.post_hooks.failure", ll_warning, lc_context)
            << "Merge of '" << src << "' to '" << dst_dir << "' post hooks returned non-zero";

    return result;
}

MergeStatusFlags
Merger::install_sym(const FSEntry & src, const FSEntry & dst_dir)
{
    Context context("When installing sym '" + stringify(src) + "' to '" + stringify(dst_dir) + "':");

    MergeStatusFlags result;

    FSEntry dst(dst_dir / src.basename());

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_install_sym_pre")
                         ("INSTALL_SOURCE", stringify(src))
                         ("INSTALL_DESTINATION", stringify(dst)))).max_exit_status)
        Log::get_instance()->message("merger.sym.pre_hooks.failure", ll_warning, lc_context)
            << "Merge of '" << src << "' to '" << dst_dir << "' pre hooks returned non-zero";

    uid_t dest_uid(src.owner() == _imp->params[k::environment()]->reduced_uid() ? 0 : src.owner());
    gid_t dest_gid(src.group() == _imp->params[k::environment()]->reduced_gid() ? 0 : src.group());

    if (0 != (src.permissions() & (S_ISVTX | S_ISUID | S_ISGID)))
        result += msi_setid_bits;

    bool do_sym(false);

    if (symlink_needs_rewriting(src))
        rewrite_symlink_as_needed(src, dst_dir);
    else
    {
        do_sym = true;
        FSCreateCon createcon(MatchPathCon::get_instance()->match(stringify(dst), S_IFLNK));
        std::pair<MergedMap::const_iterator, MergedMap::const_iterator> ii(
                _imp->merged_ids.equal_range(src.lowlevel_id()));
        for (MergedMap::const_iterator i = ii.first ; i != ii.second ; ++i)
        {
            if (0 == ::link(i->second.c_str(), stringify(dst).c_str()))
            {
                do_sym = false;
                result += msi_as_hardlink;
                break;
            }
            Log::get_instance()->message("merger.sym.link_failed", ll_debug, lc_context)
                    << "link(" << i->second + ", " << stringify(dst) << ") failed: "
                    << ::strerror(errno);
        }
    }

    if (do_sym)
    {
        FSCreateCon createcon(MatchPathCon::get_instance()->match(stringify(dst), S_IFLNK));
        if (0 != ::symlink(stringify(src.readlink()).c_str(), stringify(dst).c_str()))
            throw MergerError("Couldn't create symlink at '" + stringify(dst) + "': "
                    + stringify(::strerror(errno)));
        _imp->merged_ids.insert(make_pair(src.lowlevel_id(), stringify(dst)));
    }

    if (! _imp->params[k::no_chown()])
    {
        if (src.owner() != dest_uid || src.group() != dest_gid)
            result += msi_fixed_ownership;
        dst.lchown(dest_uid, dest_gid);
    }

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_install_sym_post")
                         ("INSTALL_SOURCE", stringify(src))
                         ("INSTALL_DESTINATION", stringify(dst)))).max_exit_status)
        Log::get_instance()->message("merger.sym.post_hooks.failure", ll_warning, lc_context) <<
            "Merge of '" << src << "' to '" << dst_dir << "' post hooks returned non-zero";

    return result;
}

void
Merger::unlink_file(FSEntry d)
{
    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_unlink_file_pre")
                         ("UNLINK_TARGET", stringify(d)))).max_exit_status)
        Log::get_instance()->message("merger.unlink_file.pre_hooks.failure", ll_warning, lc_context) <<
            "Unmerge of '" << d << "' pre hooks returned non-zero";

    if (d.is_regular_file())
    {
        mode_t mode(d.permissions());
        if ((mode & S_ISUID) || (mode & S_ISGID))
        {
            mode &= 0400;
            d.chmod(mode);
        }
    }

    d.unlink();

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_unlink_file_post")
                         ("UNLINK_TARGET", stringify(d)))).max_exit_status)
        Log::get_instance()->message("merger.unlink_file.post_hooks.failure", ll_warning, lc_context) <<
            "Unmerge of '" << d << "' post hooks returned non-zero";
}

void
Merger::unlink_sym(FSEntry d)
{
    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_unlink_sym_pre")
                         ("UNLINK_TARGET", stringify(d)))).max_exit_status)
        Log::get_instance()->message("merger.unlink_sym.pre_hooks.failure", ll_warning, lc_context) <<
            "Unmerge of '" << d << "' pre hooks returned non-zero";

    d.unlink();

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_unlink_sym_post")
                         ("UNLINK_TARGET", stringify(d)))).max_exit_status)
        Log::get_instance()->message("merger.unlink_sym.post_hooks.failure", ll_warning, lc_context) <<
            "Unmerge of '" << d << "' post hooks returned non-zero";
}

void
Merger::unlink_dir(FSEntry d)
{
    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_unlink_dir_pre")
                         ("UNLINK_TARGET", stringify(d)))).max_exit_status)
        Log::get_instance()->message("merger.unlink_dir.pre_hooks.failure", ll_warning, lc_context) <<
            "Unmerge of '" << d << "' pre hooks returned non-zero";

    d.rmdir();

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_unlink_dir_post")
                         ("UNLINK_TARGET", stringify(d)))).max_exit_status)
        Log::get_instance()->message("merger.unlink_dir.post_hooks.failure", ll_warning, lc_context) <<
            "Unmerge of '" << d << "' post hooks returned non-zero";
}

void
Merger::unlink_misc(FSEntry d)
{
    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_unlink_misc_pre")
                         ("UNLINK_TARGET", stringify(d)))).max_exit_status)
        Log::get_instance()->message("merger.unlink_misc.pre_hooks.failure", ll_warning, lc_context) <<
            "Unmerge of '" << d << "' pre hooks returned non-zero";

    d.unlink();

    if (0 != _imp->params[k::environment()]->perform_hook(extend_hook(
                         Hook("merger_unlink_misc_post")
                         ("UNLINK_TARGET", stringify(d)))).max_exit_status)
        Log::get_instance()->message("merger.unlink_misc.post_hooks.failure", ll_warning, lc_context) <<
            "Unmerge of '" << d << "' post hooks returned non-zero";
}

Hook
Merger::extend_hook(const Hook & h)
{
    return h
        ("ROOT", stringify(_imp->params[k::root()]))
        ("IMAGE", stringify(_imp->params[k::image()]));
}

#ifdef HAVE_XATTRS

void
Merger::try_to_copy_xattrs(const FSEntry & src, int dst_fd, MergeStatusFlags & flags)
{
    FDHolder src_fd(::open(stringify(src).c_str(), O_RDONLY));

    ssize_t list_sz(flistxattr(src_fd, 0, 0));
    if (-1 == list_sz)
    {
        if (ENOTSUP != errno)
            Log::get_instance()->message("merger.xattrs.failure", ll_warning, lc_context) <<
                "Got error '" << ::strerror(errno) << "' when trying to find extended attributes size for '" << src << "'";
        return;
    }

    std::tr1::shared_ptr<char> list_holder(static_cast<char *>(::operator new(list_sz)));
    list_sz = flistxattr(src_fd, list_holder.get(), list_sz);
    if (-1 == list_sz)
    {
        Log::get_instance()->message("merger.xattrs.failure", ll_warning, lc_context) <<
            "Got error '" << ::strerror(errno) << "' when trying to find extended attributes for '" << src << "'";
        return;
    }

    for (int offset(0) ; list_sz > 0 ; )
    {
        std::string key(list_holder.get() + offset);
        do
        {
            ssize_t value_sz(fgetxattr(src_fd, key.c_str(), 0, 0));
            if (-1 == value_sz)
            {
                Log::get_instance()->message("merger.xattrs.failure", ll_warning, lc_context) <<
                    "Got error '" << ::strerror(errno) << "' when trying to read size of extended attribute '" <<
                    key << "' for '" << src << "'";
                break;
            }

            std::tr1::shared_ptr<char> value_holder(static_cast<char *>(::operator new(value_sz)));
            value_sz = fgetxattr(src_fd, key.c_str(), value_holder.get(), value_sz);
            if (-1 == value_sz)
            {
                Log::get_instance()->message("merger.xattrs.failure", ll_warning, lc_context) <<
                    "Got error '" << ::strerror(errno) << "' when trying to read extended attribute '" <<
                    key << "' for '" << src << "'";
            }

            if (-1 == fsetxattr(dst_fd, key.c_str(), value_holder.get(), value_sz, 0))
            {
                if (ENOTSUP == errno)
                {
                    Log::get_instance()->message("merger.xattrs.failure", ll_warning, lc_context) <<
                        "Could not copy extended attributes from source file '" << src << "'";
                    return;
                }
                else
                    Log::get_instance()->message("merger.xattrs.failure", ll_warning, lc_context) <<
                        "Got error '" << ::strerror(errno) << "' when trying to set extended attribute '" <<
                        key << "' taken from source file '" << src << "'";
            }

            flags += msi_xattr;

        } while (false);

        list_sz -= (key.length() + 1);
        offset += (key.length() + 1);
    }
}

#else

void
Merger::try_to_copy_xattrs(const FSEntry &, int, MergeStatusFlags &)
{
}

#endif

