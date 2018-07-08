/*
 * CPathfinder.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "VCMI_Lib.h"
#include "IGameCallback.h"
#include "HeroBonus.h"
#include "int3.h"

#include <boost/heap/priority_queue.hpp>

class CGHeroInstance;
class CGObjectInstance;
struct TerrainTile;
class CPathfinderHelper;
class CMap;
class CGWhirlpool;

struct DLL_LINKAGE CGBaseNode
{
	typedef EPathfindingLayer ELayer;

	enum ENodeAction : ui8
	{
		UNKNOWN = 0,
		EMBARK = 1,
		DISEMBARK,
		NORMAL,
		BATTLE,
		VISIT,
		BLOCKING_VISIT,
		TELEPORT_NORMAL,
		TELEPORT_BLOCKING_VISIT,
		TELEPORT_BATTLE
	};

	enum EAccessibility : ui8
	{
		NOT_SET = 0,
		ACCESSIBLE = 1, //tile can be entered and passed
		VISITABLE, //tile can be entered as the last tile in path
		BLOCKVIS,  //visitable from neighbouring tile but not passable
		FLYABLE, //can only be accessed in air layer
		BLOCKED //tile can't be entered nor visited
	};

	int3 coord; //coordinates
	ELayer layer;
	EAccessibility accessible;
	ENodeAction action;
	ui32 moveRemains; //remaining tiles after hero reaches the tile
	ui8 turns; //how many turns we have to wait before reachng the tile - 0 means current turn

	CGBaseNode();
	bool reachable() const;
	bool compare(const CGBaseNode * other) const;
};

struct DLL_LINKAGE CGPathNode : public CGBaseNode
{
public:
	bool locked;
	CGPathNode * theNodeBefore;

	CGPathNode();
	void reset();
	void update(const int3 & Coord, const ELayer Layer, const EAccessibility Accessible);
	bool isLocked() const;
	void lock();
};

struct DLL_LINKAGE CHeroNode : public CGBaseNode
{
	CHeroNode * theNodeBefore;
	int mask;
	int actorNumber;
	void reset();
	void update(const int3 & Coord, const EPathfindingLayer Layer, const EAccessibility Accessible);
	void lock() {}
	bool isLocked() { return false; }
	bool isInUse() const;
};

struct DLL_LINKAGE CGPath
{
	std::vector<CGPathNode> nodes; //just get node by node

	int3 startPos() const; // start point
	int3 endPos() const; //destination point
	void convert(ui8 mode); //mode=0 -> from 'manifest' to 'object'
};

struct DLL_LINKAGE CPathsInfo
{
	typedef EPathfindingLayer ELayer;

	mutable boost::mutex pathMx;

	const CGHeroInstance * hero;
	int3 hpos;
	int3 sizes;
	boost::multi_array<CGPathNode, 4> nodes; //[w][h][level][layer]

	CPathsInfo(const int3 & Sizes);
	~CPathsInfo();
	const CGPathNode * getPathInfo(const int3 & tile) const;
	bool getPath(CGPath & out, const int3 & dst) const;
	int getDistance(const int3 & tile) const;
	const CGPathNode * getNode(const int3 & coord) const;

	CGPathNode * getNode(const int3 & coord, const ELayer layer);
};


struct PathfinderOptions
{
	bool useFlying;
	bool useWaterWalking;
	bool useEmbarkAndDisembark;
	bool useTeleportTwoWay; // Two-way monoliths and Subterranean Gate
	bool useTeleportOneWay; // One-way monoliths with one known exit only
	bool useTeleportOneWayRandom; // One-way monoliths with more than one known exit
	bool useTeleportWhirlpool; // Force enabled if hero protected or unaffected (have one stack of one creature)

							   /// TODO: Find out with client and server code, merge with normal teleporters.
							   /// Likely proper implementation would require some refactoring of CGTeleport.
							   /// So for now this is unfinished and disabled by default.
	bool useCastleGate;

	/// If true transition into air layer only possible from initial node.
	/// This is drastically decrease path calculation complexity (and time).
	/// Downside is less MP effective paths calculation.
	///
	/// TODO: If this option end up useful for slow devices it's can be improved:
	/// - Allow transition into air layer not only from initial position, but also from teleporters.
	///   Movement into air can be also allowed when hero disembarked.
	/// - Other idea is to allow transition into air within certain radius of N tiles around hero.
	///   Patrol support need similar functionality so it's won't be ton of useless code.
	///   Such limitation could be useful as it's can be scaled depend on device performance.
	bool lightweightFlyingMode;

	/// This option enable one turn limitation for flying and water walking.
	/// So if we're out of MP while cp is blocked or water tile we won't add dest tile to queue.
	///
	/// Following imitation is default H3 mechanics, but someone may want to disable it in mods.
	/// After all this limit should benefit performance on maps with tons of water or blocked tiles.
	///
	/// TODO:
	/// - Behavior when option is disabled not implemented and will lead to crashes.
	bool oneTurnSpecialLayersLimit;

	/// VCMI have different movement rules to solve flaws original engine has.
	/// If this option enabled you'll able to do following things in fly:
	/// - Move from blocked tiles to visitable one
	/// - Move from guarded tiles to blockvis tiles without being attacked
	/// - Move from guarded tiles to guarded visitable tiles with being attacked after
	/// TODO:
	/// - Option should also allow same tile land <-> air layer transitions.
	///   Current implementation only allow go into (from) air layer only to neighbour tiles.
	///   I find it's reasonable limitation, but it's will make some movements more expensive than in H3.
	bool originalMovementRules;

	PathfinderOptions();
};

template <class TPathsInfo, class TPathNode, class TNodeHelper>
class TPathfinder : private CGameInfoCallback
{
public:
	friend class CPathfinderHelper;

	TPathfinder(TPathsInfo & _out, CGameState * _gs, PlayerColor _tempOwner, std::shared_ptr<TNodeHelper> _nodeHelper);
	void calculatePaths(); //calculates possible paths for hero, uses current hero position and movement left; returns pointer to newly allocated CPath or nullptr if path does not exists

private:
	typedef EPathfindingLayer ELayer;
    PathfinderOptions options;

	TPathsInfo & out;
	PlayerColor tempOwner;
	const std::vector<std::vector<std::vector<ui8> > > &FoW;
	std::map<const CGHeroInstance*,	std::shared_ptr<CPathfinderHelper>> helpers;
	std::shared_ptr<TNodeHelper> nodeHelper;
	
	struct NodeComparer
	{
		bool operator()(const TPathNode * lhs, const TPathNode * rhs) const
		{
			return lhs->compare(rhs);
		}
	};
	boost::heap::priority_queue<TPathNode *, boost::heap::compare<NodeComparer> > pq;

	std::vector<int3> neighbourTiles;
	std::vector<int3> neighbours;

	TPathNode * cp; //current (source) path node -> we took it from the queue
	TPathNode * dp; //destination node -> it's a neighbour of cp that we consider
	const TerrainTile * ct, * dt; //tile info for both nodes
	const CGObjectInstance * ctObj, * dtObj;
	CGPathNode::ENodeAction destAction;

	void addNeighbours();
	void addTeleportExits(const CGHeroInstance * hero);
	
	bool isLayerTransitionPossible(const ELayer dstLayer, const CGHeroInstance * hero) const;
	bool isLayerTransitionPossible() const;
	bool isMovementToDestPossible(const CGHeroInstance * hero) const;
	bool isMovementAfterDestPossible(const CGHeroInstance * hero, std::shared_ptr<CPathfinderHelper> hlp) const;
	CGPathNode::ENodeAction getDestAction() const;
	CGPathNode::ENodeAction getTeleportDestAction() const;

	bool isSourceInitialPosition(const CGHeroInstance * hero) const;
	bool isSourceVisitableObj() const;
	bool isSourceGuarded(const CGHeroInstance * hero) const;
	bool isDestVisitableObj() const;
	bool isDestinationGuarded(const bool ignoreAccessibility = true) const;
	bool isDestinationGuardian() const;

	void initializeGraph();

	CGPathNode::EAccessibility evaluateAccessibility(const int3 & pos, const TerrainTile * tinfo, const ELayer layer) const;
	bool isVisitableObj(const CGObjectInstance * obj, const ELayer layer) const;
	bool canSeeObj(const CGObjectInstance * obj) const;
	bool canMoveBetween(const int3 & a, const int3 & b) const; //checks only for visitable objects that may make moving between tiles impossible, not other conditions (like tiles itself accessibility)

	bool isAllowedTeleportEntrance(const CGTeleport * obj, const CGHeroInstance * hero, std::shared_ptr<CPathfinderHelper> hlp) const;
	bool addTeleportTwoWay(const CGTeleport * obj) const;
	bool addTeleportOneWay(const CGTeleport * obj, const CGHeroInstance * hero) const;
	bool addTeleportOneWayRandom(const CGTeleport * obj, const CGHeroInstance * hero) const;
	bool addTeleportWhirlpool(const CGWhirlpool * obj, std::shared_ptr<CPathfinderHelper> hlp) const;

	std::shared_ptr<CPathfinderHelper> getHelper(const CGHeroInstance * hero);
};

struct DLL_LINKAGE TurnInfo
{
	/// This is certainly not the best design ever and certainly can be improved
	/// Unfortunately for pathfinder that do hundreds of thousands calls onus system add too big overhead
	struct BonusCache {
		std::vector<bool> noTerrainPenalty;
		bool freeShipBoarding;
		bool flyingMovement;
		int flyingMovementVal;
		bool waterWalking;
		int waterWalkingVal;

		BonusCache(TBonusListPtr bonusList);
	};
	std::unique_ptr<BonusCache> bonusCache;

	const CGHeroInstance * hero;
	TBonusListPtr bonuses;
	mutable int maxMovePointsLand;
	mutable int maxMovePointsWater;
	int nativeTerrain;

	TurnInfo(const CGHeroInstance * Hero, const int Turn = 0);
	bool isLayerAvailable(const EPathfindingLayer layer) const;
	bool hasBonusOfType(const Bonus::BonusType type, const int subtype = -1) const;
	int valOfBonuses(const Bonus::BonusType type, const int subtype = -1) const;
	int getMaxMovePoints(const EPathfindingLayer layer) const;
};

class DLL_LINKAGE CPathfinderHelper
{
public:
	CPathfinderHelper(const CGHeroInstance * Hero, const PathfinderOptions & Options);
	~CPathfinderHelper();
	void updateTurnInfo(const int turn = 0);
	bool isLayerAvailable(const EPathfindingLayer layer) const;
	const TurnInfo * getTurnInfo() const;
	bool hasBonusOfType(const Bonus::BonusType type, const int subtype = -1) const;
	int getMaxMovePoints(const EPathfindingLayer layer) const;

	static void getNeighbours(const CMap * map, const TerrainTile & srct, const int3 & tile, std::vector<int3> & vec, const boost::logic::tribool & onLand, const bool limitCoastSailing);

	static int getMovementCost(const CGHeroInstance * h, const int3 & src, const int3 & dst, const TerrainTile * ct, const TerrainTile * dt, const int remainingMovePoints =- 1, const TurnInfo * ti = nullptr, const bool checkLast = true);
	static int getMovementCost(const CGHeroInstance * h, const int3 & dst);

private:
	int turn;
	const CGHeroInstance * hero;
	std::vector<TurnInfo *> turnsInfo;
	const PathfinderOptions & options;
};

class CPathfinder
{
public:
	CPathfinder() {}
	void calculatePaths(CPathsInfo & pathsInfo, CGameState* gs, const CGHeroInstance * hero);
};

struct DLL_LINKAGE CHeroChainInfo
{
	mutable boost::mutex pathMx;

	int3 sizes;
	boost::multi_array<CHeroNode, 5> nodes; //[w][h][level][layer][chains]

	CHeroChainInfo(const int3 & Sizes);
	~CHeroChainInfo();
};

class CHeroChainConfig
{
public:
	static const int ChainLimit = 10;
	PlayerColor owner;

	CHeroChainConfig(PlayerColor owner)
		: owner(owner)
	{
	}

	virtual std::vector<CHeroNode *> getInitialNodes(CHeroChainInfo & pathsInfo) = 0;

	virtual std::vector<CHeroNode *> getNextNodes(CHeroChainInfo & pathsInfo, CHeroNode * source, int3 targetTile, EPathfindingLayer layer) = 0;

	virtual const CGHeroInstance * getNodeHero(const CHeroChainInfo & pathsInfo, const CHeroNode *) const = 0;

	virtual void updateNode(CHeroChainInfo & pathsInfo, const int3 & coord, const EPathfindingLayer layer, const CGBaseNode::EAccessibility accessible) = 0;

	bool isHeroPatrolLocked() const
	{
		return false;
	}

	bool isPatrolMovementAllowed(const int3 & dst) const
	{
		return true;
	}

	bool isPatrolEnabled() const
	{
		return false;
	}
};

class CHeroChainFinder
{
public:
	CHeroChainFinder() {}
	void calculatePaths(CHeroChainInfo & pathsInfo, CGameState* gs, std::shared_ptr<CHeroChainConfig> config);
};
