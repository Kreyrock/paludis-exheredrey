/* vim: set sw=4 sts=4 et foldmethod=syntax : */

/*
 * Copyright (c) 2007, 2008 Ciaran McCreesh
 * Copyright (c) 2008 David Leverton
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

#include "homepage_key.hh"
#include <paludis/repositories/fake/fake_repository.hh>
#include <paludis/repositories/fake/fake_package_id.hh>
#include <paludis/environments/test/test_environment.hh>
#include <paludis/package_database.hh>
#include <paludis/qa.hh>
#include <test/test_framework.hh>
#include <test/test_runner.hh>

using namespace paludis;
using namespace paludis::erepository;
using namespace test;

namespace
{
    struct TestReporter :
        QAReporter
    {
        unsigned count;
        std::string last_message;

        TestReporter() :
            count(0)
        {
        }

        void message(const QAMessage & m)
        {
            ++count;
            last_message = m.message;
        }

        void status(const std::string &)
        {
        }
    };
}

namespace test_cases
{
    struct GoodTest : TestCase
    {
        GoodTest() : TestCase("good") { }

        void run()
        {
            TestEnvironment env;
            std::tr1::shared_ptr<FakeRepository> repo(new FakeRepository(&env, RepositoryName("repo")));
            env.package_database()->add_repository(1, repo);
            std::tr1::shared_ptr<FakePackageID> id(repo->add_version("cat", "pkg", "1"));
            id->homepage_key()->set_from_string("http://paludis.pioto.org/");

            TestReporter r;
            TEST_CHECK(homepage_key_check(FSEntry("/var/empty"), r, id, "homepage_key"));
            TEST_CHECK_EQUAL(r.count, 0u);
        }
    } test_good;

    struct BadTest : TestCase
    {
        BadTest() : TestCase("bad") { }

        void run()
        {
            TestEnvironment env;
            std::tr1::shared_ptr<FakeRepository> repo(new FakeRepository(&env, RepositoryName("repo")));
            env.package_database()->add_repository(1, repo);
            std::tr1::shared_ptr<FakePackageID> id(repo->add_version("cat", "pkg", "1"));
            id->homepage_key()->set_from_string("ptth://paludis.pioto.org/");

            TestReporter r;
            TEST_CHECK(homepage_key_check(FSEntry("/var/empty"), r, id, "homepage_key"));
            TEST_CHECK_EQUAL(r.count, 1u);
            TEST_CHECK_EQUAL(r.last_message, "Homepage uses no or unknown protocol in part 'ptth://paludis.pioto.org/'");
        }
    } test_bad;

    struct EmptyTest : TestCase
    {
        EmptyTest() : TestCase("empty") { }

        void run()
        {
            TestEnvironment env;
            std::tr1::shared_ptr<FakeRepository> repo(new FakeRepository(&env, RepositoryName("repo")));
            env.package_database()->add_repository(1, repo);
            std::tr1::shared_ptr<FakePackageID> id(repo->add_version("cat", "pkg", "1"));
            id->homepage_key()->set_from_string("( )");

            TestReporter r;
            TEST_CHECK(homepage_key_check(FSEntry("/var/empty"), r, id, "homepage_key"));
            TEST_CHECK_EQUAL(r.count, 1u);
            TEST_CHECK_EQUAL(r.last_message, "Homepage specifies no URIs");
        }
    } test_empty;
}

