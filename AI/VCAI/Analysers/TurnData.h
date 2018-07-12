/*
 * Goals.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "lib/CPathfinder.h"
#include "CHeroChainConfig.h"

struct CHeroChainPathNode
{
	const CGHeroInstance * hero;
	int3 targetPosition;
	int movementPointsLeft;
	int movementPointsUsed;
	int turns;
	uint64_t armyLoss;
	uint64_t armyValue;
	CGBaseNode::ENodeAction action;
	EPathfindingLayer layer;
};

struct CHeroChainPath
{
	std::vector<CHeroChainPathNode> nodes;

	CHeroChainPath() 
		: nodes({}) 
	{
	}
};

struct TurnData
{
	const CHeroChainInfo * chainInfo;
	const std::shared_ptr<CVCAIHeroChainConfig> chainConfig;

	TurnData(PlayerColor playerID);
	void update();
	std::vector<CHeroChainPath> getChainInfo(int3 pos);
};

