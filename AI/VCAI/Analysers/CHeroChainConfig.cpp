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

extern boost::thread_specific_ptr<CCallback> cb;

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

bool shouldCancelNode(CHeroChainInfo & pathsInfo, CHeroNode * node)
{
	auto coord = node->coord;
	auto layer = node->layer;

	for(int i = 0; i < CVCAIHeroChainConfig::ChainLimit; i++)
	{
		CHeroNode * otherNode = &pathsInfo.nodes[coord.x][coord.y][coord.z][layer][i];

		if(otherNode->armyValue >= node->armyValue)
		{
			if(otherNode->turns < node->turns
				|| otherNode->turns == node->turns && otherNode->moveRemains < node->moveRemains)
			{
				return true;
			}
		}

		if(otherNode->armyValue > node->armyValue
			&& otherNode->turns == node->turns && otherNode->moveRemains <= node->moveRemains)
		{
			return true;
		}
	}

	return false;
}

std::vector<CHeroNode *> CVCAIHeroChainConfig::getNextNodes(CHeroChainInfo & pathsInfo, CHeroNode * source, int3 targetTile, EPathfindingLayer layer)
{
	std::vector<CHeroNode *> result;
	auto singleHero = (source->mask & (~BATTLE_NODE)) == (1 << source->actorNumber);

	if(source->turns > 1 && !singleHero)
	{
		return result; // restrict scan depth
	}

	auto heroNode = allocateHeroNode(pathsInfo, targetTile, layer, source->mask, source->actorNumber);

	if(heroNode == nullptr)
		return result;

	assert(heroNode->actorNumber < actors.size());
	assert(heroNode->actorNumber >= 0);
	assert(heroNode->coord.x >= 0);
	assert(heroNode->coord.x < 300);

	if(!shouldCancelNode(pathsInfo, heroNode))
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

		if(!heroNode.isInUse() && heroNode.accessible != CGBaseNode::NOT_SET)
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

void CVCAIHeroChainConfig::apply(
	CHeroNode * node,
	int turns,
	int remains,
	CGBaseNode::ENodeAction destAction,
	CHeroNode * parent,
	CGBaseNode::ENodeBlocker blocker)
{
	node->moveRemains = remains;
	node->turns = turns;
	node->action = destAction;
	node->previousActor = parent->previousActor;
	node->armyValue = parent->armyValue;
	node->armyLoss = parent->armyLoss;

	if(blocker == CGBaseNode::ENodeBlocker::SOURCE_GUARDED)
	{
		auto srcGuardian = cb->guardingCreaturePosition(parent->coord);

		if(srcGuardian == parent->coord)
		{
			node->previousActor = parent;
		}
	}
	
	if(parent->action == CGBaseNode::ENodeAction::BLOCKING_VISIT || parent->action == CGBaseNode::ENodeAction::VISIT)
	{
		// we can not directly bypass objects, we need to interact with them first
		node->previousActor = parent;
	}

	assert(node->armyValue > 0);
	assert(node->previousActor != node);
}

const CGObjectInstance * getGuardian(int3 tile)
{
	auto guardian = cb->guardingCreaturePosition(tile);
	if(!guardian.valid())
	{
		return nullptr;
	}

	auto topObj = cb->getTopObj(tile);
	if(topObj->ID != Obj::MONSTER)
	{
		return nullptr;
	}

	return topObj;
}

CHeroNode * CVCAIHeroChainConfig::tryBypassBlocker(
	CHeroChainInfo & paths,
	CHeroNode * source,
	CHeroNode * dest,
	CGBaseNode::ENodeBlocker blocker)
{
	auto hero = getNodeHero(paths, source);

	if(blocker == CHeroNode::DESTINATION_BLOCKVIS)
	{
		auto obj = cb->getTopObj(dest->coord);
		
		if(!obj)
			return nullptr;

		if(obj->ID == Obj::RESOURCE || obj->ID == Obj::ARTIFACT || obj->ID == Obj::TREASURE_CHEST
			|| obj->ID == Obj::SEA_CHEST || obj->ID == Obj::CAMPFIRE || obj->ID == Obj::PANDORAS_BOX)
		{
			return dest;
		}
	}

	if(blocker == CHeroNode::DESTINATION_VISIT)
	{
		return dest;
	}

	if(blocker == CHeroNode::SOURCE_GUARDED && (source->mask & BATTLE_NODE) > 0)
	{
		auto srcGuardians = cb->getGuardingCreatures(source->coord);
		auto destGuardians = cb->getGuardingCreatures(dest->coord);

		for(auto srcGuard : srcGuardians)
		{
			if(!vstd::contains(destGuardians, srcGuard))
				continue;

			auto guardPos = srcGuard->visitablePos();
			if(guardPos != source->coord && guardPos != dest->coord)
				return nullptr;
		}

		return dest;
	}

	if(blocker == CHeroNode::DESTINATION_GUARDED)
	{
		auto srcGuardians = cb->getGuardingCreatures(source->coord);
		auto destGuardians = cb->getGuardingCreatures(dest->coord);

		if(destGuardians.empty())
			return nullptr;

		vstd::erase_if(destGuardians, [&](const CGObjectInstance * destGuard) -> bool
		{
			return vstd::contains(srcGuardians, destGuard);
		});

		auto guardsAlreadyBypassed = destGuardians.empty() && srcGuardians.size();
		if(guardsAlreadyBypassed && (source->mask & BATTLE_NODE) > 0)
		{
			return dest;
		}

		auto battleNode = allocateHeroNode(paths, dest->coord, dest->layer, dest->mask | BATTLE_NODE, dest->actorNumber);
		auto loss = evaluateLoss(hero, dest->coord, source->armyValue);

		if(battleNode != nullptr && source->armyValue > loss
			&& battleNode->armyValue < source->armyValue - loss
			&& isBetterWay(battleNode, source, dest->moveRemains, dest->turns))
		{
			apply(battleNode, dest->turns, dest->moveRemains, dest->action, source, blocker);

			battleNode->armyLoss = source->armyLoss + loss;
			battleNode->armyValue = source->armyValue - loss;

			return battleNode;
		}
	}

	return nullptr;
}
