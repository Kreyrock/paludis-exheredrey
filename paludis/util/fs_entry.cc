/* vim: set sw=4 sts=4 et foldmethod=syntax : */

/*
 * Copyright (c) 2005, 2006, 2007 Ciaran McCreesh <ciaranm@ciaranm.org>
 * Copyright (c) 2006 Mark Loeser <halcy0n@gentoo.org>
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

#include <paludis/util/exception.hh>
#include <paludis/util/fs_entry.hh>
#include <paludis/util/stringify.hh>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <cstring>
#include <cstdlib>
#include "config.h"

/** \file
 * Implementation of paludis::FSEntry.
 *
 * \ingroup grpfilesystem
 */

using namespace paludis;

FSError::FSError(const std::string & our_message) throw () :
    Exception(our_message)
{
}

FSEntry::FSEntry(const std::string & path) :
    _path(path),
    _exists(false),
    _checked(false)
{
    _normalise();
}

FSEntry::FSEntry(const FSEntry & other) :
    _path(other._path),
    _stat_info(other._stat_info),
    _exists(other._exists),
    _checked(other._checked)
{
}

FSEntry::~FSEntry()
{
}

const FSEntry &
FSEntry::operator= (const FSEntry & other)
{
    _path = other._path;
    _stat_info = other._stat_info;
    _exists = other._exists;
    _checked = other._checked;

    return *this;
}

const FSEntry &
FSEntry::operator/= (const FSEntry & rhs)
{
    if (_path.empty() || '/' != _path.at(_path.length() - 1))
        _path.append("/");

    if (! rhs._path.empty())
    {
        if ('/' == rhs._path.at(0))
            _path.append(rhs._path.substr(1));
        else
            _path.append(rhs._path);
    }

    _checked = false;
    _exists = false;
    _stat_info.reset();

    return *this;
}

FSEntry
FSEntry::operator/ (const std::string & rhs) const
{
    return *this / FSEntry(rhs);
}

bool
FSEntry::operator< (const FSEntry & other) const
{
    return _path < other._path;
}

bool
FSEntry::operator== (const FSEntry & other) const
{
    return _path == other._path;
}

bool
FSEntry::exists() const
{
    _stat();

    return _exists;
}

bool
FSEntry::is_directory() const
{
    _stat();

    if (_exists)
        return S_ISDIR((*_stat_info).st_mode);

    return false;
}

bool
FSEntry::is_directory_or_symlink_to_directory() const
{
    _stat();

    if (_exists)
        return S_ISDIR((*_stat_info).st_mode) ||
            (is_symbolic_link() && realpath_if_exists().is_directory());

    return false;
}

bool
FSEntry::is_fifo() const
{
    _stat();

    if (_exists)
        return S_ISFIFO((*_stat_info).st_mode);

    return false;
}

bool
FSEntry::is_device() const
{
    _stat();

    if (_exists)
        return S_ISBLK((*_stat_info).st_mode) || S_ISCHR((*_stat_info).st_mode);

    return false;
}

bool
FSEntry::is_regular_file() const
{
    _stat();

    if (_exists)
        return S_ISREG((*_stat_info).st_mode);

    return false;
}

bool
FSEntry::is_regular_file_or_symlink_to_regular_file() const
{
    _stat();

    if (_exists)
        return S_ISREG((*_stat_info).st_mode) ||
            (is_symbolic_link() && realpath_if_exists().is_regular_file());

    return false;
}

bool
FSEntry::is_symbolic_link() const
{
    _stat();

    if (_exists)
        return S_ISLNK((*_stat_info).st_mode);

    return false;
}


bool
FSEntry::has_permission(const FSUserGroup & user_group, const FSPermission & fs_perm) const
{
    Context context("When checking permissions on '" + stringify(_path) + "':");

    _stat();

    if (! _exists)
        throw FSError("Filesystem entry '" + _path + "' does not exist");

    switch (user_group)
    {
        case fs_ug_owner:
            {
                switch (fs_perm)
                {
                    case fs_perm_read:
                        return (*_stat_info).st_mode & S_IRUSR;
                    case fs_perm_write:
                        return (*_stat_info).st_mode & S_IWUSR;
                    case fs_perm_execute:
                        return (*_stat_info).st_mode & S_IXUSR;
                }
                throw InternalError(PALUDIS_HERE, "Unhandled FSPermission");
            }
        case fs_ug_group:
            {
                switch (fs_perm)
                {
                    case fs_perm_read:
                        return (*_stat_info).st_mode & S_IRGRP;
                    case fs_perm_write:
                        return (*_stat_info).st_mode & S_IWGRP;
                    case fs_perm_execute:
                        return (*_stat_info).st_mode & S_IXGRP;
                }
                throw InternalError(PALUDIS_HERE, "Unhandled FSPermission");
            }
        case fs_ug_others:
            {
                switch (fs_perm)
                {
                    case fs_perm_read:
                        return (*_stat_info).st_mode & S_IROTH;
                    case fs_perm_write:
                        return (*_stat_info).st_mode & S_IWOTH;
                    case fs_perm_execute:
                        return (*_stat_info).st_mode & S_IXOTH;
                }
                throw InternalError(PALUDIS_HERE, "Unhandled FSPermission");
            }
    }

    throw InternalError(PALUDIS_HERE, "Unhandled FSUserGroup");
}

mode_t
FSEntry::permissions() const
{
    Context context("When fetching permissions for '" + stringify(_path) + "':");

    _stat();

    if (! _exists)
        throw FSError("Filesystem entry '" + _path + "' does not exist");

    return _stat_info->st_mode;
}

void
FSEntry::_normalise()
{
    try
    {
        if (std::string::npos != _path.find("//"))
        {
            std::string new_path;
            std::string::size_type p(0);
            while (p < _path.length())
            {
                if ('/' == _path[p])
                {
                    new_path += '/';
                    while (++p < _path.length())
                        if ('/' != _path[p])
                            break;
                }
                else
                    new_path += _path[p++];
            }
            _path = new_path;
        }

        if (! _path.empty())
            if ('/' == _path.at(_path.length() - 1))
                _path.erase(_path.length() - 1);
        if (_path.empty())
            _path = "/";
    }
    catch (const std::exception & e)
    {
        Context c("When normalising FSEntry path '" + _path + "':");
        throw InternalError(PALUDIS_HERE,
                "caught std::exception '" + stringify(e.what()) + "'");
    }
}

void
FSEntry::_stat() const
{
    if (_checked)
        return;

    Context context("When calling stat() on '" + stringify(_path) + "':");

    _stat_info.reset(new struct stat);
    if (0 != lstat(_path.c_str(), _stat_info.get()))
    {
        if (errno != ENOENT)
            throw FSError("Error running stat() on '" + stringify(_path) + "': "
                    + strerror(errno));

        _exists = false;
        _stat_info.reset();
    }
    else
        _exists = true;

    _checked = true;
}

std::string
FSEntry::basename() const
{
    if (_path == "/")
        return _path;

    return _path.substr(_path.rfind('/') + 1);
}

FSEntry
FSEntry::strip_leading(const FSEntry & f) const
{
    std::string root(stringify(f));

    if (root == "/")
        root.clear();
    if (0 != _path.compare(0, root.length(), root))
        throw FSError("Can't strip leading '" + root + "' from FSEntry '" + _path + "'");
    return FSEntry(_path.substr(root.length()));
}

FSEntry
FSEntry::dirname() const
{
    if (_path == "/")
        return FSEntry(_path);

    return FSEntry(_path.substr(0, _path.rfind('/')));
}

FSEntry
FSEntry::realpath() const
{
    Context context("When fetching realpath of '" + stringify(_path) + "':");

#ifdef HAVE_CANONICALIZE_FILE_NAME
    char * r(canonicalize_file_name(_path.c_str()));
    if (! r)
        throw FSError("Could not resolve path '" + _path + "'");
    FSEntry result(r);
    std::free(r);
    return result;
#else
    char r[PATH_MAX + 1];
    std::memset(r, 0, PATH_MAX + 1);
    if (! exists())
        throw FSError("Could not resolve path '" + _path + "'");
    if (! ::realpath(_path.c_str(), r))
        throw FSError("Could not resolve path '" + _path + "'");
    FSEntry result(r);
    if (! result.exists())
        throw FSError("Could not resolve path '" + _path + "'");
    return result;
#endif
}

FSEntry
FSEntry::realpath_if_exists() const
{
    Context context("When fetching realpath of '" + stringify(_path) + "', if it exists:");

#ifdef HAVE_CANONICALIZE_FILE_NAME
    char * r(canonicalize_file_name(_path.c_str()));
    if (! r)
        return *this;
    FSEntry result(r);
    std::free(r);
    return result;
#else
    char r[PATH_MAX + 1];
    std::memset(r, 0, PATH_MAX + 1);
    if (! exists())
        return *this;
    if (! ::realpath(_path.c_str(), r))
        return *this;
    FSEntry result(r);
    if (! result.exists())
        return *this;
    return result;
#endif
}

FSEntry
FSEntry::cwd()
{
    char r[PATH_MAX + 1];
    std::memset(r, 0, PATH_MAX + 1);
    if (! ::getcwd(r, PATH_MAX))
        throw FSError("Could not get current working directory");
    return FSEntry(r);
}

std::ostream &
paludis::operator<< (std::ostream & s, const FSEntry & f)
{
    s << f._path;
    return s;
}

time_t
FSEntry::ctime() const
{
    _stat();

    if (! _exists)
        throw FSError("Filesystem entry '" + _path + "' does not exist");

    return (*_stat_info).st_ctime;
}

time_t
FSEntry::mtime() const
{
    _stat();

    if (! _exists)
        throw FSError("Filesystem entry '" + _path + "' does not exist");

    return (*_stat_info).st_mtime;
}

off_t
FSEntry::file_size() const
{
    _stat();

    if (! _exists)
        throw FSError("Filesystem entry '" + _path + "' does not exist");

    if (! is_regular_file())
        throw FSError("file_size called on non-regular file '" + _path + "'");

    return _stat_info->st_size;
}

bool
FSEntry::mkdir(mode_t mode)
{
    if (0 == ::mkdir(_path.c_str(), mode))
        return true;

    int e(errno);
    if (e == EEXIST)
    {
        if (is_directory())
            return false;
        throw FSError("mkdir '" + _path + "' failed: target exists and is not a directory");
    }
    else
        throw FSError("mkdir '" + _path + "' failed: " + ::strerror(e));
}

bool
FSEntry::unlink()
{
#ifdef HAVE_LCHFLAGS
    if (0 != ::lchflags(_path.c_str(), 0))
    {
        int e(errno);
        if (e != ENOENT)
            throw FSError("lchflags for unlink '" + _path + "' failed: " + ::strerror(e));
    }
#endif

    if (0 == ::unlink(_path.c_str()))
        return true;

    int e(errno);
    if (e == ENOENT)
        return false;
    else
        throw FSError("unlink '" + _path + "' failed: " + ::strerror(e));
}

bool
FSEntry::rmdir()
{
    if (0 == ::rmdir(_path.c_str()))
        return true;

    int e(errno);
    if (e == ENOENT)
        return false;
    else
        throw FSError("rmdir '" + _path + "' failed: " + ::strerror(e));
}

std::string
FSEntry::readlink() const
{
    char buf[PATH_MAX + 1];
    std::memset(buf, 0, PATH_MAX + 1);
    if (-1 == ::readlink(_path.c_str(), buf, PATH_MAX))
        throw FSError("readlink '" + _path + "' failed: " + ::strerror(errno));
    return buf;
}

void
FSEntry::chown(const uid_t new_owner, const gid_t new_group)
{
    if (0 != ::chown(_path.c_str(), new_owner, new_group))
        throw FSError("chown '" + _path + "' to '" + stringify(new_owner) + "', '"
                + stringify(new_group) + "' failed: " + ::strerror(errno));
}

void
FSEntry::chmod(const mode_t mode)
{
    if (0 != ::chmod(_path.c_str(), mode))
        throw FSError("chmod '" + _path + "' failed: " + ::strerror(errno));
}

uid_t
FSEntry::owner() const
{
    _stat();

    if (! _exists)
        throw FSError("Filesystem entry '" + _path + "' does not exist");

    return _stat_info->st_uid;
}

gid_t
FSEntry::group() const
{
    _stat();

    if (! _exists)
        throw FSError("Filesystem entry '" + _path + "' does not exist");

    return _stat_info->st_gid;
}

void
FSEntry::rename(const FSEntry & new_name)
{
    if (0 != ::rename(_path.c_str(), new_name._path.c_str()))
        throw FSError("rename('" + stringify(_path) + "', '" + stringify(new_name._path) + "') failed: " +
                ::strerror(errno));
}

