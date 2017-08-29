/*
    This file is part of solidity.

    solidity is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    solidity is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * Unit tests for the view and pure checker.
 */

#include <test/libsolidity/AnalysisFramework.h>

#include <boost/test/unit_test.hpp>

#include <string>

using namespace std;

namespace dev
{
namespace solidity
{
namespace test
{

BOOST_FIXTURE_TEST_SUITE(ViewPureChecker, AnalysisFramework)

BOOST_AUTO_TEST_CASE(smoke_test)
{
	char const* text = R"(
		contract C {
			uint x;
			function g()) pure {}
			function f() returns (uint) view { return now; }
			function h() { x = 2; }
			function i() payable { x = 2; }
		}
	)";
	CHECK_SUCCESS_NO_WARNINGS(text);
}

BOOST_AUTO_TEST_CASE(call_internal_functions_success)
{
	char const* text = R"(
		contract C {
			function g() pure { g(); f(); }
			function f() returns (uint) view { f(); }
			function h() { h(); g(); f(); }
			function i() payable { i(); h(); g(); f(); }
		}
	)";
	CHECK_SUCCESS_NO_WARNINGS(text);
}

BOOST_AUTO_TEST_CASE(suggest_pure)
{
	char const* text = R"(
		contract C {
			function g() view { }
		}
	)";
	CHECK_WARNING(text, "changed to pure");
}

BOOST_AUTO_TEST_CASE(suggest_view)
{
	char const* text = R"(
		contract C {
			uint x;
			function g() returns (uint) { return x; }
		}
	)";
	CHECK_WARNING(text, "changed to view");
}

BOOST_AUTO_TEST_CASE(call_internal_functions_fail)
{
	CHECK_ERROR("contract C{ function f() pure { g(); } function g() view {} }", TypeError, "aoeu");
}


// TODO: function types,
/*contract Ballot {
	struct S {
		uint x;
	}
	S public s;
	function f() internal returns (S storage) {
		return s;
	}
	function g()
	{
		f().x = 2;
	}
}*/
// TODO modifiers
// TODO check that we do not get a suggestion for interfaces and also not for overriding functions
// TODO as a special case: reading from a struct-valued storage variable itself is fine,
// just not reading one of its members.

BOOST_AUTO_TEST_SUITE_END()

}
}
}
