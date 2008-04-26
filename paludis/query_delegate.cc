/* vim: set sw=4 sts=4 et foldmethod=syntax : */

/*
 * Copyright (c) 2007, 2008 Ciaran McCreesh
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

#include <paludis/query_delegate.hh>

using namespace paludis;

QueryDelegate::QueryDelegate()
{
}

QueryDelegate::~QueryDelegate()
{
}

std::tr1::shared_ptr<RepositoryNameSequence>
QueryDelegate::repositories(const Environment &) const
{
    return std::tr1::shared_ptr<RepositoryNameSequence>();
}

std::tr1::shared_ptr<CategoryNamePartSet>
QueryDelegate::categories(const Environment &, std::tr1::shared_ptr<const RepositoryNameSequence>) const
{
    return std::tr1::shared_ptr<CategoryNamePartSet>();
}

std::tr1::shared_ptr<QualifiedPackageNameSet>
QueryDelegate::packages(const Environment &, std::tr1::shared_ptr<const RepositoryNameSequence>,
        std::tr1::shared_ptr<const CategoryNamePartSet>) const
{
    return std::tr1::shared_ptr<QualifiedPackageNameSet>();
}

std::tr1::shared_ptr<PackageIDSequence>
QueryDelegate::ids(const Environment &, std::tr1::shared_ptr<const RepositoryNameSequence>,
        std::tr1::shared_ptr<const QualifiedPackageNameSet>) const
{
    return std::tr1::shared_ptr<PackageIDSequence>();
}


