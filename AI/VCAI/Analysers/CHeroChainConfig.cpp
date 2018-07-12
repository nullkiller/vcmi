/*
 * Goals.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CHeroChainConfig.h"
#include "../VCAI.h"

std::vector<CHeroNode *> CVCAIHeroChainConfig::getInitialNodes(CHeroChainInfo & pathsInfo)
{
	std::vector<CHeroNode *> result;

	for(int i = 0; i < actors.size(); i++)
	{
		const CGHeroInstance * hero = actors[i].hero;
		int3 position = hero->getPosition(false);
		EPathfindingLayer layer = hero->boat ? EPathfindingLayer::SAIL : EPathfindingLayer::LAND;

		auto mask = 1 << i;
		auto heroNode = allocateHeroNode(pathsInfo, position, layer, mask, i);

		heroNode->moveRemains = hero->movement;
		heroNode->turns = 0;
		heroNode->armyValue = hero->getArmyStrength();
		heroNode->armyLoss = 0;

		result.push_back(heroNode);
	}

	return result;
}

std::vector<CHeroNode *> CVCAIHeroChainConfig::getNextNodes(CHeroChainInfo & pathsInfo, CHeroNode * source, int3 targetTile, EPathfindingLayer layer)
{
	std::vector<CHeroNode *> result;

	if(source->turns > 1)
	{
		return result; // restrict scan depth
	}

	auto heroNode = allocateHeroNode(pathsInfo, targetTile, layer, source->mask, source->actorNumber);

	if(heroNode == nullptr)
		return result;

	if(heroNode->accessible == CGBaseNode::NOT_SET)
	{
		heroNode->reset();
		return result;
	}

	assert(heroNode->actorNumber < actors.size());
	assert(heroNode->actorNumber >= 0);
	assert(heroNode->coord.x >= 0);
	assert(heroNode->coord.x < 300);

	result.push_back(heroNode);

	return result;
}

const CGHeroInstance * CVCAIHeroChainConfig::getNodeHero(const CHeroChainInfo & pathsInfo, const CHeroNode * source) const
{
	return actors[source->actorNumber].hero;
}

void CVCAIHeroChainConfig::updateNode(CHeroChainInfo & pathsInfo, const int3 & coord, const EPathfindingLayer layer, const CGBaseNode::EAccessibility accessible)
{
	for(int i = 0; i < ChainLimit; i++)
	{
		CHeroNode & heroNode = pathsInfo.nodes[coord.x][coord.y][coord.z][layer][i];

		heroNode.update(coord, layer, accessible);
	}
}

void CVCAIHeroChainConfig::addHero(const CGHeroInstance * hero)
{
	actors.push_back(CHeroChainActor(hero));
}

void CVCAIHeroChainConfig::reset()
{
	actors.clear();
}

CHeroNode * CVCAIHeroChainConfig::allocateHeroNode(CHeroChainInfo & paths, int3 coord, EPathfindingLayer layer, int mask, int actorNumber)
{
	for(int i = 0; i < ChainLimit; i++)
	{
		CHeroNode & heroNode = paths.nodes[coord.x][coord.y][coord.z][layer][i];

		if(!heroNode.isInUse())
		{
			heroNode.mask = mask;
			heroNode.actorNumber = actorNumber;

			return &heroNode;
		}

		if(heroNode.mask == mask && heroNode.actorNumber == actorNumber)
			return &heroNode;
	}

	return nullptr;
}

bool CVCAIHeroChainConfig::isBetterWay(CHeroNode * target, CHeroNode * source, int remains, int turn)
{
	if(source->armyValue > target->armyValue)
		return true;
	else if(target->turns == 0xff) //we haven't been here before
		return true;
	else if(target->turns > turn)
		return true;
	else if(target->turns >= turn && target->moveRemains < remains) //this route is faster
		return true;

	return false;
};

void CVCAIHeroChainConfig::apply(CHeroNode * node, int turns, int remains, CGBaseNode::ENodeAction destAction, CHeroNode * parent)
{
	node->moveRemains = remains;
	node->turns = turns;
	node->action = destAction;

	if(node->previousActor != parent)
	{
		node->previousActor = parent->previousActor;
		node->armyValue = parent->armyValue;
		node->armyLoss = parent->armyLoss;
	}
}

CHeroNode * CVCAIHeroChainConfig::tryBypassObject(CHeroChainInfo & paths, CHeroNode * node, const CGObjectInstance * obj)
{
	auto hero = getNodeHero(paths, node);

	if(obj && obj->ID == Obj::MONSTER)
	{
		auto monsterNode = allocateHeroNode(paths, obj->visitablePos(), node->layer, node->mask, node->actorNumber);
		auto battleNode = allocateHeroNode(paths,node->coord, node->layer, node->mask | BATTLE_NODE, node->actorNumber);
		auto loss = evaluateLoss(hero, monsterNode->coord, monsterNode->armyValue);

		if(monsterNode != nullptr && monsterNode->action != CGBaseNode::ENodeAction::UNKNOWN
			&& battleNode != nullptr && battleNode != monsterNode && battleNode->action == CGBaseNode::ENodeAction::UNKNOWN
			&& monsterNode->armyValue > loss)
		{
			battleNode->armyLoss = monsterNode->armyLoss + loss;
			battleNode->previousActor = monsterNode;
			battleNode->armyValue = monsterNode->armyValue - loss;

			return battleNode;
		}
	}

	return nullptr; // not supported by regular pathfinder
}
