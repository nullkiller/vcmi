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

struct CHeroChainActor
{
	const CGHeroInstance * hero;

	CHeroChainActor(const CGHeroInstance * hero)
		:hero(hero)
	{
	}
};

class CVCAIHeroChainConfig : public CHeroChainConfig
{
public:
	static const uint32_t BATTLE_NODE = 0x80000000;
	std::vector<CHeroChainActor> actors;

	CVCAIHeroChainConfig(PlayerColor owner)
		:CHeroChainConfig(owner)
	{
	}

	std::vector<CHeroNode *> getInitialNodes(CHeroChainInfo & pathsInfo);

	std::vector<CHeroNode *> getNextNodes(CHeroChainInfo & pathsInfo, CHeroNode * source, int3 targetTile, EPathfindingLayer layer);

	const CGHeroInstance * getNodeHero(const CHeroChainInfo & pathsInfo, const CHeroNode * source) const;

	void updateNode(CHeroChainInfo & pathsInfo, const int3 & coord, const EPathfindingLayer layer, const CGBaseNode::EAccessibility accessible);

	bool isBetterWay(CHeroNode * target, CHeroNode * source, int remains, int turn);

	void apply(
		CHeroNode * node,
		int turns,
		int remains,
		CGBaseNode::ENodeAction destAction,
		CHeroNode * parent,
		CGBaseNode::ENodeBlocker blocker);

	CHeroNode * tryBypassBlocker(
		CHeroChainInfo & paths,
		CHeroNode * source,
		CHeroNode * dest,
		CGBaseNode::ENodeBlocker blocker);

	void addHero(const CGHeroInstance * hero);
	
	void reset(); 

private:
	CHeroNode * allocateHeroNode(CHeroChainInfo & paths, int3 coord, EPathfindingLayer layer, int mask, int actorNumber);
};