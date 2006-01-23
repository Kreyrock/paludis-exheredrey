/* vim: set sw=4 sts=4 et foldmethod=syntax : */

/*
 * Copyright (c) 2006 Stephen Bennett <spb@gentoo.org>
 *
 * This file is part of the Paludis package manager. Paludis is free software;
 * you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
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

#ifndef PALUDIS_GUARD_PALUDIS_DESTRINGIFY_HH
#define PALUDIS_GUARD_PALUDIS_DESTRINGIFY_HH 1

#include <string>
#include <sstream>
#include <paludis/exception.hh>

/** \file
 * Destringify functions.
 */

namespace paludis
{
    /**
     * Default exception type thrown by destringify when it fails.
     */
    class DestringifyError : public Exception
    {
        public:
            /**
             * Constructor.
             */
            DestringifyError(const std::string & str) throw ();
    };

    namespace destringify_internals
    {
        template <typename Type_, typename Exception_>
        struct Destringifier
        {
            static Type_ do_destringify(const std::string & s)
            {
                std::istringstream ss(s);
                Type_ t;
                ss >> t;
                if (!ss.eof() || ss.bad())
                    throw Exception_(s);
                return t;
            }
        };

        template <typename Exception_>
        struct Destringifier<std::string, Exception_>
        {
            static std::string do_destringify(const std::string & s)
            {
                return s;
            }
        };

        template <typename Exception_>
        struct Destringifier<bool, Exception_>
        {
            static bool do_destringify(const std::string & s)
            {
                int i;
                try
                {
                    i = Destringifier<int, Exception_>::do_destringify(s);
                    return i > 0;
                }
                catch(DestringifyError &)
                {
                    bool b;
                    std::istringstream ss(s);
                    ss >> std::boolalpha >> b;
                    if(!ss.eof() || ss.bad())
                        throw Exception_(s);
                    return b;
                }
            }
        };
    }

    /**
     * Extract a value of some type from a string.
     */
    template <typename Type_, typename Exception_>
    Type_ destringify(const std::string & s)
    {
        return destringify_internals::Destringifier<Type_, Exception_>::do_destringify(s);
    }

    template <typename Type_>
    Type_ destringify(const std::string & s)
    {
        return destringify<Type_, DestringifyError>(s);
    }
}

#endif
