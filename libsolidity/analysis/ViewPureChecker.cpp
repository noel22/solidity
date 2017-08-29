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

#include <libsolidity/analysis/ViewPureChecker.h>

using namespace std;
using namespace dev;
using namespace dev::solidity;

bool ViewPureChecker::check()
{
	vector<ContractDefinition const*> contracts;

	for (auto const& node: m_ast)
	{
		SourceUnit const* source = dynamic_cast<SourceUnit const*>(node.get());
		solAssert(source, "");
		for (auto const& topLevelNode: source->nodes())
		{
			ContractDefinition const* contract = dynamic_cast<ContractDefinition const*>(topLevelNode.get());
			if (contract)
				contracts.push_back(contract);
		}
	}

	// Check modifiers first to infer their state mutability.
	for (auto const* contract: contracts)
		for (ModifierDefinition const* mod: contract->functionModifiers())
			mod->accept(*this);

	for (auto const* contract: contracts)
		contract->accept(*this);

	return !m_errors;
}



bool ViewPureChecker::visit(FunctionDefinition const& _funDef)
{
	solAssert(!m_currentFunction, "");
	m_currentFunction = &_funDef;
	m_currentBestMutability = StateMutability::Pure;
	return true;
}

void ViewPureChecker::endVisit(FunctionDefinition const& _funDef)
{
	solAssert(m_currentFunction == &_funDef, "");
	if (
		m_currentBestMutability < _funDef.stateMutability() &&
		_funDef.stateMutability() != StateMutability::Payable &&
		_funDef.isImplemented() &&
		!_funDef.isConstructor() &&
		!_funDef.annotation().superFunction
	)
		m_errorReporter.warning(
			_funDef.location(),
			"Function state mutability can be restricted to " + stateMutabilityToString(m_currentBestMutability)
		);
	m_currentFunction = nullptr;
}

bool ViewPureChecker::visit(ModifierDefinition const&)
{
	solAssert(m_currentFunction == nullptr, "");
	m_currentBestMutability = StateMutability::Pure;
	return true;
}

void ViewPureChecker::endVisit(ModifierDefinition const& _modifierDef)
{
	solAssert(m_currentFunction == nullptr, "");
	m_inferredMutability[&_modifierDef] = m_currentBestMutability;
}

void ViewPureChecker::endVisit(Identifier const& _identifier)
{
	Declaration const* declaration = _identifier.annotation().referencedDeclaration;
	solAssert(declaration, "");

	StateMutability mutability = StateMutability::Pure;

	bool writes = _identifier.annotation().lValueRequested;
	if (VariableDeclaration const* varDecl = dynamic_cast<VariableDeclaration const*>(declaration))
	{
		if (varDecl->isStateVariable())
			mutability = writes ? StateMutability::NonPayable : StateMutability::View;
	}
	else if (MagicVariableDeclaration const* magicVar = dynamic_cast<MagicVariableDeclaration const*>(declaration))
	{
		switch (magicVar->type()->category())
		{
		case Type::Category::Contract:
			solAssert(_identifier.name() == "this" || _identifier.name() == "super", "");
			if (!dynamic_cast<ContractType const&>(*magicVar->type()).isSuper())
				// reads the address
				mutability = StateMutability::View;
			break;
		case Type::Category::Integer:
			solAssert(_identifier.name() == "now", "");
			mutability = StateMutability::View;
			break;
		default:
			break;
		}
	}

	reportMutability(mutability, _identifier);
}

void ViewPureChecker::endVisit(InlineAssembly const& _inlineAssembly)
{
	// @TOOD we can and should analyze it further.
	reportMutability(StateMutability::NonPayable, _inlineAssembly);
}

void ViewPureChecker::reportMutability(StateMutability _mutability, ASTNode const& _node)
{
	if (m_currentFunction && m_currentFunction->stateMutability() < _mutability)
	{
		m_errors = true;
		string text;
		if (_mutability == StateMutability::View)
			text =
				"Function declared as pure, but this expression reads from the "
				"environment and thus requires \"view\".";
		else if (_mutability == StateMutability::NonPayable)
			text =
				"Function declared as " +
				stateMutabilityToString(m_currentFunction->stateMutability()) +
				", but this expression modifies the state and thus "
				"requires non-payable (the default) or payable.";
		else
			solAssert(false, "");

		if (m_currentFunction->stateMutability() == StateMutability::View)
			// Change this to error with 0.5.0
			m_errorReporter.warning(_node.location(), text);
		else if (m_currentFunction->stateMutability() == StateMutability::Pure)
			m_errorReporter.typeError(_node.location(), text);
		else
			solAssert(false, "");
	}
	if (_mutability > m_currentBestMutability)
		m_currentBestMutability = _mutability;
}

void ViewPureChecker::endVisit(FunctionCall const& _functionCall)
{
	if (_functionCall.annotation().kind != FunctionCallKind::FunctionCall)
		return;

	StateMutability mut = dynamic_cast<FunctionType const&>(*_functionCall.expression().annotation().type).stateMutability();
	// We only require "nonpayable" to call a payble function.
	if (mut == StateMutability::Payable)
		mut = StateMutability::NonPayable;
	reportMutability(mut, _functionCall);
}

void ViewPureChecker::endVisit(MemberAccess const& _memberAccess)
{
	StateMutability mutability = StateMutability::Pure;
	bool writes = _memberAccess.annotation().lValueRequested;

	ASTString const& member = _memberAccess.memberName();
	switch (_memberAccess.expression().annotation().type->category())
	{
	case Type::Category::Contract:
	case Type::Category::Integer:
		if (member == "balance" && !_memberAccess.annotation().referencedDeclaration)
			mutability = StateMutability::View;
		break;
	case Type::Category::Magic:
		// we can ignore the kind of magic and only look at the name of the member
		if (member != "data" && member != "sig" && member != "value")
			mutability = StateMutability::View;
		break;
	case Type::Category::Struct:
	{
		if (_memberAccess.expression().annotation().type->dataStoredIn(DataLocation::Storage))
			mutability = writes ? StateMutability::NonPayable : StateMutability::View;
		break;
	}
	case Type::Category::Array:
	{
		auto const& type = dynamic_cast<ArrayType const&>(*_memberAccess.expression().annotation().type);
		if (member == "length" && type.isDynamicallySized() && type.dataStoredIn(DataLocation::Storage))
			mutability = writes ? StateMutability::NonPayable : StateMutability::View;
		break;
	}
	default:
		break;
	}
	reportMutability(mutability, _memberAccess);
}

void ViewPureChecker::endVisit(IndexAccess const& _indexAccess)
{
	solAssert(_indexAccess.indexExpression(), "");

	bool writes = _indexAccess.annotation().lValueRequested;
	if (_indexAccess.baseExpression().annotation().type->dataStoredIn(DataLocation::Storage))
		reportMutability(writes ? StateMutability::NonPayable : StateMutability::View, _indexAccess);
}

void ViewPureChecker::endVisit(ModifierInvocation const& _modifier)
{
	solAssert(_modifier.name(), "");
	ModifierDefinition const* mod = dynamic_cast<decltype(mod)>(_modifier.name()->annotation().referencedDeclaration);
	solAssert(mod, "");
	solAssert(m_inferredMutability.count(mod), "");

	reportMutability(m_inferredMutability.at(mod), _modifier);
}

