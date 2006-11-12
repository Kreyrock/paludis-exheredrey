#!/usr/bin/ruby
# vim: set sw=4 sts=4 et tw=80 :

#
# Copyright (c) 2006 Ciaran McCreesh <ciaranm@ciaranm.org>
#
# This file is part of the Paludis package manager. Paludis is free software;
# you can redistribute it and/or modify it under the terms of the GNU General
# Public License version 2, as published by the Free Software Foundation.
#
# Paludis is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA  02111-1307  USA
#

ENV["PALUDIS_HOME"] = Dir.getwd().to_s + "/environment_TEST_dir/home";

require 'test/unit'
require 'Paludis'

Paludis::Log.instance.log_level = Paludis::LogLevel::Warning

module Paludis

    class TestCase_DefaultEnvironment < Test::Unit::TestCase
        def test_instance
            assert_equal DefaultEnvironment.instance.__id__, DefaultEnvironment.instance.__id__
            assert_kind_of Environment, DefaultEnvironment.instance
            assert_kind_of DefaultEnvironment, DefaultEnvironment.instance
        end

        def test_no_create
            assert_raise NoMethodError do
                x = DefaultEnvironment.new()
            end
        end
    end

    class TestCase_NoConfigEnvironment < Test::Unit::TestCase
        def test_create
            e = NoConfigEnvironment.new(Dir.getwd().to_s + "/environment_TEST_dir/testrepo")
            assert_kind_of Environment, e
            assert_kind_of NoConfigEnvironment, e

            e = NoConfigEnvironment.new(Dir.new(Dir.getwd().to_s + "/environment_TEST_dir/testrepo"))
            assert_kind_of Environment, e
            assert_kind_of NoConfigEnvironment, e

            assert_raise TypeError do
                e = NoConfigEnvironment.new(123)
            end
        end
    end

    class TestCase_DefaultEnvironmentUse < Test::Unit::TestCase
        def test_query_use
            assert DefaultEnvironment.instance.query_use("enabled")
            assert ! DefaultEnvironment.instance.query_use("not_enabled")
            assert ! DefaultEnvironment.instance.query_use("sometimes_enabled")

            pde = PackageDatabaseEntry.new("foo/bar", VersionSpec.new("1.0"), "testrepo")

            assert DefaultEnvironment.instance.query_use("enabled", pde)
            assert ! DefaultEnvironment.instance.query_use("not_enabled", pde)
            assert DefaultEnvironment.instance.query_use("sometimes_enabled", pde)
        end

        def test_query_use_bad
            assert_raise ArgumentError do
                DefaultEnvironment.instance.query_use(1, 2, 3)
            end
            assert_raise TypeError do
                DefaultEnvironment.instance.query_use(123)
            end
        end
    end

    class TestCase_NoConfigEnvironmentUse < Test::Unit::TestCase
        def env
            NoConfigEnvironment.new(Dir.getwd().to_s + "/environment_TEST_dir/testrepo")
        end

        def test_query_use
            assert ! env.query_use("foo")
            pde = PackageDatabaseEntry.new("foo/bar", VersionSpec.new("1.0"), "testrepo")
            assert ! env.query_use("foo", pde)
        end

        def test_query_use_bad
            assert_raise ArgumentError do
                env.query_use(1, 2, 3)
            end
            assert_raise TypeError do
                env.query_use(123)
            end
        end
    end

    class TestCase_DefaultEnvironmentAcceptKeyword < Test::Unit::TestCase
        def test_accept_keyword
            assert DefaultEnvironment.instance.accept_keyword("test")
            assert ! DefaultEnvironment.instance.accept_keyword("bad")
            assert ! DefaultEnvironment.instance.accept_keyword("~test")

            pde = PackageDatabaseEntry.new("foo/bar", "1.0", "testrepo")

            assert DefaultEnvironment.instance.accept_keyword("test", pde)
            assert ! DefaultEnvironment.instance.accept_keyword("bad", pde)
            assert DefaultEnvironment.instance.accept_keyword("~test", pde)
        end

        def test_accept_keyword_bad
            assert_raise ArgumentError do
                DefaultEnvironment.instance.accept_keyword(1, 2, 3)
            end
            assert_raise TypeError do
                DefaultEnvironment.instance.accept_keyword(123)
            end
        end
    end

    class TestCase_NoConfigEnvironmentAcceptKeyword < Test::Unit::TestCase
        def env
            NoConfigEnvironment.new(Dir.getwd().to_s + "/environment_TEST_dir/testrepo")
        end

        def test_accept_keyword
            assert env.accept_keyword("test")
            assert ! env.accept_keyword("bad")
            assert ! env.accept_keyword("~test")

            pde = PackageDatabaseEntry.new("foo/bar", "1.0", "testrepo")

            assert env.accept_keyword("test", pde)
            assert ! env.accept_keyword("bad", pde)
            assert ! env.accept_keyword("~test", pde)
        end

        def test_accept_keyword_bad
            assert_raise ArgumentError do
                env.accept_keyword(1, 2, 3)
            end
            assert_raise TypeError do
                env.accept_keyword(123)
            end
        end
    end

    class TestCase_DefaultEnvironmentAcceptLicense < Test::Unit::TestCase
        def test_accept_license
            assert DefaultEnvironment.instance.accept_license("test")

            pde = PackageDatabaseEntry.new("foo/bar", VersionSpec.new("1.0"), "testrepo")

            assert DefaultEnvironment.instance.accept_license("test", pde)
        end

        def test_accept_license_bad
            assert_raise ArgumentError do
                DefaultEnvironment.instance.accept_license(1, 2, 3)
            end
            assert_raise TypeError do
                DefaultEnvironment.instance.accept_license(123)
            end
        end
    end

    class TestCase_NoConfigEnvironmentAcceptLicense < Test::Unit::TestCase
        def env
            NoConfigEnvironment.new(Dir.getwd().to_s + "/environment_TEST_dir/testrepo")
        end

        def test_accept_license
            assert env.accept_license("test")
            pde = PackageDatabaseEntry.new("foo/bar", VersionSpec.new("1.0"), "testrepo")
            assert env.accept_license("test", pde)
        end

        def test_accept_license_bad
            assert_raise ArgumentError do
                env.accept_license(1, 2, 3)
            end
            assert_raise TypeError do
                env.accept_license(123)
            end
        end
    end

    class TestCase_NoConfigEnvironmentMaskReasons < Test::Unit::TestCase
        def env
            NoConfigEnvironment.new(Dir.getwd().to_s + "/environment_TEST_dir/testrepo")
        end

        def test_mask_reasons
            p = PackageDatabaseEntry.new("foo/bar", VersionSpec.new("1.0"), "testrepo")

            m = env.mask_reasons(p)
            assert m.empty?
        end

        def test_mask_reasons_not_empty
            p = PackageDatabaseEntry.new("foo/bar", VersionSpec.new("2.0"), "testrepo")

            m = env.mask_reasons(p)
            assert ! m.empty?
            assert m.include?("keyword")
            assert_equal ["keyword"], m.to_a
        end

        def test_mask_reasons_no_such_repo
            p = PackageDatabaseEntry.new("foo/bar", VersionSpec.new("1.0"), "nosuchrepo")

            assert_raise Paludis::NoSuchRepositoryError do
                env.mask_reasons p
            end
        end

        def test_mask_reasons_bad
            assert_raise ArgumentError do
                env.mask_reasons(1, 2)
            end
            assert_raise TypeError do
                env.mask_reasons(123)
            end
        end
    end

    class TestCase_DefaultEnvironmentPackageDatabase < Test::Unit::TestCase
        def db
            DefaultEnvironment.instance.package_database
        end

        def test_package_database
            assert_kind_of PackageDatabase, db
            assert_equal "testrepo", db.fetch_repository("testrepo").name
        end
    end

    class TestCase_NoConfigEnvironmentPackageDatabase < Test::Unit::TestCase
        def env
            NoConfigEnvironment.new(Dir.getwd().to_s + "/environment_TEST_dir/testrepo")
        end

        def db
            env.package_database
        end

        def test_package_database
            assert_kind_of PackageDatabase, db
            assert_equal "testrepo", db.fetch_repository("testrepo").name
        end
    end

    class TestCase_DefaultEnvironmentPackageSet < Test::Unit::TestCase
        def test_package_set
            assert_kind_of DepAtom, DefaultEnvironment.instance.package_set('everything')
        end

        def test_package_set_error
            assert_raise SetNameError do
                DefaultEnvironment.instance.package_set('broken*')
            end
        end
    end

    class TestCase_NoConfigEnvironmentPackageSet < Test::Unit::TestCase
        def env
            NoConfigEnvironment.new(Dir.getwd().to_s + "/environment_TEST_dir/testrepo")
        end

        def test_package_set
            assert_kind_of DepAtom, DefaultEnvironment.instance.package_set('everything')
        end

        def test_package_set_error
            assert_raise SetNameError do
                env.package_set('broken*')
            end
        end
    end
end

