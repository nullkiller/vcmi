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
#include "Explore.h"
#include "CaptureObjects.h"
#include "../VCAI.h"
#include "../SectorMap.h"
#include "lib/mapping/CMap.h" //for victory conditions
#include "lib/CPathfinder.h"
#include "../Tasks/VisitTile.h"
#include "../Tasks/BuildStructure.h"
#include "../Tasks/RecruitHero.h"

extern boost::thread_specific_ptr<CCallback> cb;
extern boost::thread_specific_ptr<VCAI> ai;

using namespace Goals;

Tasks::TaskList Explore::getTasks() {
	Tasks::TaskList tasks = Tasks::TaskList();

	if (!hero->movement) {
		return tasks;
	}

	addTasks(tasks, sptr(CaptureObjects(this->getExplorationHelperObjects()).sethero(hero)), 0.8);

	int3 t = whereToExplore(hero);
	if (t.valid())
	{
		addTask(tasks, Tasks::VisitTile(t, hero), 0.3);
	}
	else
	{
		t = explorationDesperate(hero);

		if (t.valid()) //don't waste time if we are completely blocked
			addTasks(tasks, sptr(Goals::ClearWayTo(t, hero)), 0); // TODO: Implement ClearWayTo.getTasks
	}

	return tasks;
}

std::vector<const CGObjectInstance *> Explore::getExplorationHelperObjects() {
	//try to use buildings that uncover map
	std::vector<const CGObjectInstance *> objs;
	for (auto obj : ai->visitableObjs)
	{
		if (!vstd::contains(ai->alreadyVisited, obj))
		{
			switch (obj->ID.num)
			{
			case Obj::REDWOOD_OBSERVATORY:
			case Obj::PILLAR_OF_FIRE:
			case Obj::CARTOGRAPHER:
				objs.push_back(obj);
				break;
			case Obj::MONOLITH_ONE_WAY_ENTRANCE:
			case Obj::MONOLITH_TWO_WAY:
			case Obj::SUBTERRANEAN_GATE:
				auto tObj = dynamic_cast<const CGTeleport *>(obj);
				assert(ai->knownTeleportChannels.find(tObj->channel) != ai->knownTeleportChannels.end());
				if (TeleportChannel::IMPASSABLE != ai->knownTeleportChannels[tObj->channel]->passability)
					objs.push_back(obj);
				break;
			}
		}
		else
		{
			switch (obj->ID.num)
			{
			case Obj::MONOLITH_TWO_WAY:
			case Obj::SUBTERRANEAN_GATE:
				auto tObj = dynamic_cast<const CGTeleport *>(obj);
				if (TeleportChannel::IMPASSABLE == ai->knownTeleportChannels[tObj->channel]->passability)
					break;
				for (auto exit : ai->knownTeleportChannels[tObj->channel]->exits)
				{
					if (!cb->getObj(exit))
					{ // Always attempt to visit two-way teleports if one of channel exits is not visible
						objs.push_back(obj);
						break;
					}
				}
				break;
			}
		}
	}

	return objs;
}

int3 Explore::explorationBestNeighbour(int3 hpos, int radius, HeroPtr h)
{
	std::map<int3, double> dstToRevealedTiles;

	for(crint3 dir : int3::getDirs())
	{
		int3 tile = hpos + dir;
		if(cb->isInTheMap(tile))
		{
			if(isBlockVisitObj(tile))
				continue;

			if(isSafeToVisit(h, tile) && isAccessibleForHero(tile, h))
			{
				double distance = distanceToTile(h.get(), tile); // diagonal movement opens more tiles but spends more mp
				dstToRevealedTiles[tile] = howManyTilesWillBeDiscovered(tile, radius, cb.get(), h) / distance;
			}
		}
	}

	if(dstToRevealedTiles.empty()) //yes, it DID happen!
		throw cannotFulfillGoalException("No neighbour will bring new discoveries!");

	auto best = dstToRevealedTiles.begin();
	for(auto i = dstToRevealedTiles.begin(); i != dstToRevealedTiles.end(); i++)
	{
		const CGPathNode * pn = cb->getPathsInfo(h.get())->getPathInfo(i->first);
		//const TerrainTile *t = cb->getTile(i->first);
		if(best->second < i->second && pn->reachable() && pn->accessible == CGPathNode::ACCESSIBLE)
			best = i;
	}

	if(best->second)
		return best->first;

	throw cannotFulfillGoalException("No neighbour will bring new discoveries!");
}

int3 Explore::explorationNewPoint(HeroPtr h)
{
	int radius = h->getSightRadius();
	CCallback * cbp = cb.get();
	const CGHeroInstance * hero = h.get();

	std::vector<std::vector<int3>> tiles; //tiles[distance_to_fow]
	tiles.resize(radius);

	foreach_tile_pos([&](const int3 & pos)
	{
		if(!cbp->isVisible(pos))
			tiles[0].push_back(pos);
	});

	float bestValue = 0; //discovered tile to node distance ratio
	int3 bestTile(-1, -1, -1);
	int3 ourPos = h->convertPosition(h->pos, false);
	const CPathsInfo* pathsInfo = cbp->getPathsInfo(hero);

	for(int i = 1; i < radius; i++)
	{
		getVisibleNeighbours(tiles[i - 1], tiles[i]);
		vstd::removeDuplicates(tiles[i]);

		for(const int3 & tile : tiles[i])
		{
			if(tile == ourPos) //shouldn't happen, but it does
				continue;
			if(!pathsInfo->getPathInfo(tile)->reachable()) //this will remove tiles that are guarded by monsters (or removable objects)
				continue;

			CGPath path;
			cb->getPathsInfo(hero)->getPath(path, tile);
			float ourValue = (float)howManyTilesWillBeDiscovered(tile, radius, cbp, h) / (path.nodes.size() + 1); //+1 prevents erratic jumps

			if(ourValue > bestValue) //avoid costly checks of tiles that don't reveal much
			{
				if(isSafeToVisit(h, tile))
				{
					if(isBlockVisitObj(tile)) //we can't stand on that object
						continue;
					bestTile = tile;
					bestValue = ourValue;
				}
			}
		}
	}
	return bestTile;
}

int3 Explore::explorationDesperate(HeroPtr h)
{
	auto sm = ai->getCachedSectorMap(h);
	int radius = h->getSightRadius();

	std::vector<std::vector<int3>> tiles; //tiles[distance_to_fow]
	tiles.resize(radius);

	CCallback * cbp = cb.get();
	const CPathsInfo* paths = cbp->getPathsInfo(h.get());

	foreach_tile_pos([&](const int3 & pos)
	{
		if(!cbp->isVisible(pos))
			tiles[0].push_back(pos);
	});

	ui64 lowestDanger = -1;
	int3 bestTile(-1, -1, -1);

	for(int i = 1; i < radius; i++)
	{
		getVisibleNeighbours(tiles[i - 1], tiles[i]);
		vstd::removeDuplicates(tiles[i]);

		for(const int3 & tile : tiles[i])
		{
			if(cbp->getTile(tile)->blocked) //does it shorten the time?
				continue;
			if(!howManyTilesWillBeDiscovered(tile, radius, cbp, h)) //avoid costly checks of tiles that don't reveal much
				continue;

			auto t = sm->firstTileToGet(h, tile);
			if(t.valid())
			{
				ui64 ourDanger = evaluateDanger(t, h.h);
				if(ourDanger < lowestDanger)
				{
					if(!isBlockVisitObj(t))
					{
						if(!ourDanger) //at least one safe place found
							return t;

						bestTile = t;
						lowestDanger = ourDanger;
					}
				}
			}
		}
	}
	return bestTile;
}

int3 Explore::whereToExplore(HeroPtr h)
{
	TimeCheck tc("where to explore");
	int radius = h->getSightRadius();
	int3 hpos = h->visitablePos();

	try //check if nearby tiles allow us to reveal anything - this is quick
	{
		return explorationBestNeighbour(hpos, radius, h);
	}
	catch(cannotFulfillGoalException & e)
	{
		//perform exhaustive search
		return explorationNewPoint(h);
	}
}