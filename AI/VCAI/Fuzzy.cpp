/*
 * Fuzzy.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
*/
#include "StdInc.h"
#include "Fuzzy.h"
#include <limits>

#include "../../lib/mapObjects/MapObjects.h"
#include "../../lib/mapObjects/CommonConstructors.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPathfinder.h"
#include "../../lib/CGameStateFwd.h"
#include "../../lib/VCMI_Lib.h"
#include "../../CCallback.h"
#include "VCAI.h"
#include "SectorMap.h"

#define MIN_AI_STRENGHT (0.5f) //lower when combat AI gets smarter
#define UNGUARDED_OBJECT (100.0f) //we consider unguarded objects 100 times weaker than us

struct BankConfig;
class CBankInfo;
class Engine;
class InputVariable;
class CGTownInstance;

FuzzyHelper * fh;

extern boost::thread_specific_ptr<CCallback> cb;
extern boost::thread_specific_ptr<VCAI> ai;

engineBase::engineBase()
{
	engine.addRuleBlock(&rules);
}

void engineBase::configure()
{
	engine.configure("Minimum", "Maximum", "Minimum", "AlgebraicSum", "Centroid", "General");
	logAi->info(engine.toString());
}

void engineBase::addRule(const std::string & txt)
{
	rules.addRule(fl::Rule::parse(txt, &engine));
}

struct armyStructure
{
	float walkers, shooters, flyers;
	ui32 maxSpeed;
};

armyStructure evaluateArmyStructure(const CArmedInstance * army)
{
	ui64 totalStrenght = army->getArmyStrength();
	double walkersStrenght = 0;
	double flyersStrenght = 0;
	double shootersStrenght = 0;
	ui32 maxSpeed = 0;

	for(auto s : army->Slots())
	{
		bool walker = true;
		if(s.second->type->hasBonusOfType(Bonus::SHOOTER))
		{
			shootersStrenght += s.second->getPower();
			walker = false;
		}
		if(s.second->type->hasBonusOfType(Bonus::FLYING))
		{
			flyersStrenght += s.second->getPower();
			walker = false;
		}
		if(walker)
			walkersStrenght += s.second->getPower();

		vstd::amax(maxSpeed, s.second->type->valOfBonuses(Bonus::STACKS_SPEED));
	}
	armyStructure as;
	as.walkers = walkersStrenght / totalStrenght;
	as.shooters = shootersStrenght / totalStrenght;
	as.flyers = flyersStrenght / totalStrenght;
	as.maxSpeed = maxSpeed;
	assert(as.walkers || as.flyers || as.shooters);
	return as;
}

FuzzyHelper::FuzzyHelper()
{
	initTacticalAdvantage();
	ta.configure();
	initVisitTile();
	vt.configure();
}


void FuzzyHelper::initTacticalAdvantage()
{
	try
	{
		ta.ourShooters = new fl::InputVariable("OurShooters");
		ta.ourWalkers = new fl::InputVariable("OurWalkers");
		ta.ourFlyers = new fl::InputVariable("OurFlyers");
		ta.enemyShooters = new fl::InputVariable("EnemyShooters");
		ta.enemyWalkers = new fl::InputVariable("EnemyWalkers");
		ta.enemyFlyers = new fl::InputVariable("EnemyFlyers");

		//Tactical advantage calculation
		std::vector<fl::InputVariable *> helper =
		{
			ta.ourShooters, ta.ourWalkers, ta.ourFlyers, ta.enemyShooters, ta.enemyWalkers, ta.enemyFlyers
		};

		for(auto val : helper)
		{
			ta.engine.addInputVariable(val);
			val->addTerm(new fl::Ramp("FEW", 0.6, 0.0));
			val->addTerm(new fl::Ramp("MANY", 0.4, 1));
			val->setRange(0.0, 1.0);
		}

		ta.ourSpeed = new fl::InputVariable("OurSpeed");
		ta.enemySpeed = new fl::InputVariable("EnemySpeed");

		helper = {ta.ourSpeed, ta.enemySpeed};

		for(auto val : helper)
		{
			ta.engine.addInputVariable(val);
			val->addTerm(new fl::Ramp("LOW", 6.5, 3));
			val->addTerm(new fl::Triangle("MEDIUM", 5.5, 10.5));
			val->addTerm(new fl::Ramp("HIGH", 8.5, 16));
			val->setRange(0, 25);
		}

		ta.castleWalls = new fl::InputVariable("CastleWalls");
		ta.engine.addInputVariable(ta.castleWalls);
		{
			fl::Rectangle * none = new fl::Rectangle("NONE", CGTownInstance::NONE, CGTownInstance::NONE + (CGTownInstance::FORT - CGTownInstance::NONE) * 0.5f);
			ta.castleWalls->addTerm(none);

			fl::Trapezoid * medium = new fl::Trapezoid("MEDIUM", (CGTownInstance::FORT - CGTownInstance::NONE) * 0.5f, CGTownInstance::FORT,
								   CGTownInstance::CITADEL, CGTownInstance::CITADEL + (CGTownInstance::CASTLE - CGTownInstance::CITADEL) * 0.5f);
			ta.castleWalls->addTerm(medium);

			fl::Ramp * high = new fl::Ramp("HIGH", CGTownInstance::CITADEL - 0.1, CGTownInstance::CASTLE);
			ta.castleWalls->addTerm(high);

			ta.castleWalls->setRange(CGTownInstance::NONE, CGTownInstance::CASTLE);
		}


		ta.bankPresent = new fl::InputVariable("Bank");
		ta.engine.addInputVariable(ta.bankPresent);
		{
			fl::Rectangle * termFalse = new fl::Rectangle("FALSE", 0.0, 0.5f);
			ta.bankPresent->addTerm(termFalse);
			fl::Rectangle * termTrue = new fl::Rectangle("TRUE", 0.5f, 1);
			ta.bankPresent->addTerm(termTrue);
			ta.bankPresent->setRange(0, 1);
		}

		ta.threat = new fl::OutputVariable("Threat");
		ta.engine.addOutputVariable(ta.threat);
		ta.threat->addTerm(new fl::Ramp("LOW", 1, MIN_AI_STRENGHT));
		ta.threat->addTerm(new fl::Triangle("MEDIUM", 0.8, 1.2));
		ta.threat->addTerm(new fl::Ramp("HIGH", 1, 1.5));
		ta.threat->setRange(MIN_AI_STRENGHT, 1.5);

		ta.addRule("if OurShooters is MANY and EnemySpeed is LOW then Threat is LOW");
		ta.addRule("if OurShooters is MANY and EnemyShooters is FEW then Threat is LOW");
		ta.addRule("if OurSpeed is LOW and EnemyShooters is MANY then Threat is HIGH");
		ta.addRule("if OurSpeed is HIGH and EnemyShooters is MANY then Threat is LOW");

		ta.addRule("if OurWalkers is FEW and EnemyShooters is MANY then Threat is somewhat LOW");
		ta.addRule("if OurShooters is MANY and EnemySpeed is HIGH then Threat is somewhat HIGH");
		//just to cover all cases
		ta.addRule("if OurShooters is FEW and EnemySpeed is HIGH then Threat is MEDIUM");
		ta.addRule("if EnemySpeed is MEDIUM then Threat is MEDIUM");
		ta.addRule("if EnemySpeed is LOW and OurShooters is FEW then Threat is MEDIUM");

		ta.addRule("if Bank is TRUE and OurShooters is MANY then Threat is somewhat HIGH");
		ta.addRule("if Bank is TRUE and EnemyShooters is MANY then Threat is LOW");

		ta.addRule("if CastleWalls is HIGH and OurWalkers is MANY then Threat is very HIGH");
		ta.addRule("if CastleWalls is HIGH and OurFlyers is MANY and OurShooters is MANY then Threat is MEDIUM");
		ta.addRule("if CastleWalls is MEDIUM and OurShooters is MANY and EnemyWalkers is MANY then Threat is LOW");

	}
	catch(fl::Exception & pe)
	{
		logAi->error("initTacticalAdvantage: %s", pe.getWhat());
	}
}

ui64 FuzzyHelper::estimateBankDanger(const CBank * bank)
{
	//this one is not fuzzy anymore, just calculate weighted average

	auto objectInfo = VLC->objtypeh->getHandlerFor(bank->ID, bank->subID)->getObjectInfo(bank->appearance);

	CBankInfo * bankInfo = dynamic_cast<CBankInfo *>(objectInfo.get());

	ui64 totalStrength = 0;
	ui8 totalChance = 0;
	for(auto config : bankInfo->getPossibleGuards())
	{
		totalStrength += config.second.totalStrength * config.first;
		totalChance += config.first;
	}
	return totalStrength / std::max<ui8>(totalChance, 1); //avoid division by zero

}

float FuzzyHelper::getTacticalAdvantage(const CArmedInstance * we, const CArmedInstance * enemy)
{
	float output = 1;
	try
	{
		armyStructure ourStructure = evaluateArmyStructure(we);
		armyStructure enemyStructure = evaluateArmyStructure(enemy);

		ta.ourWalkers->setValue(ourStructure.walkers);
		ta.ourShooters->setValue(ourStructure.shooters);
		ta.ourFlyers->setValue(ourStructure.flyers);
		ta.ourSpeed->setValue(ourStructure.maxSpeed);

		ta.enemyWalkers->setValue(enemyStructure.walkers);
		ta.enemyShooters->setValue(enemyStructure.shooters);
		ta.enemyFlyers->setValue(enemyStructure.flyers);
		ta.enemySpeed->setValue(enemyStructure.maxSpeed);

		bool bank = dynamic_cast<const CBank *>(enemy);
		if(bank)
			ta.bankPresent->setValue(1);
		else
			ta.bankPresent->setValue(0);

		const CGTownInstance * fort = dynamic_cast<const CGTownInstance *>(enemy);
		if(fort)
			ta.castleWalls->setValue(fort->fortLevel());
		else
			ta.castleWalls->setValue(0);

		//engine.process(TACTICAL_ADVANTAGE);//TODO: Process only Tactical_Advantage
		ta.engine.process();
		output = ta.threat->getValue();
	}
	catch(fl::Exception & fe)
	{
		logAi->error("getTacticalAdvantage: %s ", fe.getWhat());
	}

	if(output < 0 || (output != output))
	{
		fl::InputVariable * tab[] = {ta.bankPresent, ta.castleWalls, ta.ourWalkers, ta.ourShooters, ta.ourFlyers, ta.ourSpeed, ta.enemyWalkers, ta.enemyShooters, ta.enemyFlyers, ta.enemySpeed};
		std::string names[] = {"bankPresent", "castleWalls", "ourWalkers", "ourShooters", "ourFlyers", "ourSpeed", "enemyWalkers", "enemyShooters", "enemyFlyers", "enemySpeed" };
		std::stringstream log("Warning! Fuzzy engine doesn't cover this set of parameters: ");

		for(int i = 0; i < boost::size(tab); i++)
			log << names[i] << ": " << tab[i]->getValue() << " ";
		logAi->error(log.str());
		assert(false);
	}

	return output;
}

FuzzyHelper::TacticalAdvantage::~TacticalAdvantage()
{
	//TODO: smart pointers?
	delete ourWalkers;
	delete ourShooters;
	delete ourFlyers;
	delete enemyWalkers;
	delete enemyShooters;
	delete enemyFlyers;
	delete ourSpeed;
	delete enemySpeed;
	delete bankPresent;
	delete castleWalls;
	delete threat;
}

//std::shared_ptr<AbstractGoal> chooseSolution (std::vector<std::shared_ptr<AbstractGoal>> & vec)

Goals::TSubgoal FuzzyHelper::chooseSolution(Goals::TGoalVec vec)
{
	if(vec.empty()) //no possibilities found
		return sptr(Goals::Invalid());

	ai->cachedSectorMaps.clear();

	//a trick to switch between heroes less often - calculatePaths is costly
	auto sortByHeroes = [](const Goals::TSubgoal & lhs, const Goals::TSubgoal & rhs) -> bool
	{
		return lhs->hero.h < rhs->hero.h;
	};
	boost::sort(vec, sortByHeroes);

	for(auto g : vec)
	{
		setPriority(g);
	}

	auto compareGoals = [](const Goals::TSubgoal & lhs, const Goals::TSubgoal & rhs) -> bool
	{
		return lhs->priority < rhs->priority;
	};
	boost::sort(vec, compareGoals);

	return vec.back();
}

float FuzzyHelper::evaluate(Goals::RecruitHero & g)
{
	return 1; //just try to recruit hero as one of options
}
FuzzyHelper::EvalVisitTile::~EvalVisitTile()
{
	delete armyLossPersentage;
	delete heroStrength;
	delete turnDistance;
	delete missionImportance;
	delete goldReward;
}

void FuzzyHelper::initVisitTile()
{
	try
	{
		vt.armyLossPersentage = new fl::InputVariable("armyLoss"); //hero must be strong enough to defeat guards
		vt.heroStrength = new fl::InputVariable("heroStrength"); //we want to use weakest possible hero
		vt.turnDistance = new fl::InputVariable("turnDistance"); //we want to use hero who is near
		vt.missionImportance = new fl::InputVariable("lockedMissionImportance"); //we may want to preempt hero with low-priority mission
		vt.goldReward = new fl::InputVariable("goldReward"); //indicate AI that content of the file is important or it is probably bad
		vt.armyReward = new fl::InputVariable("armyReward"); //indicate AI that content of the file is important or it is probably bad
		vt.value = new fl::OutputVariable("Value");
		vt.value->setMinimum(0);
		vt.value->setMaximum(1);

		std::vector<fl::InputVariable *> helper = {vt.armyLossPersentage, vt.heroStrength, vt.turnDistance, vt.missionImportance, vt.goldReward};
		for(auto val : helper)
		{
			vt.engine.addInputVariable(val);
		}
		vt.engine.addOutputVariable(vt.value);

		vt.armyLossPersentage->addTerm(new fl::Ramp("LOW", 0.2, 0));
		vt.armyLossPersentage->addTerm(new fl::Triangle("MEDIUM", 0.1, 0.3));
		vt.armyLossPersentage->addTerm(new fl::Ramp("HIGH", 0.2, 1));
		vt.armyLossPersentage->setRange(0, 1);

		//strength compared to our main hero
		vt.heroStrength->addTerm(new fl::Ramp("LOW", 0.2, 0));
		vt.heroStrength->addTerm(new fl::Triangle("MEDIUM", 0.2, 0.8));
		vt.heroStrength->addTerm(new fl::Ramp("HIGH", 0.5, 1));
		vt.heroStrength->setRange(0.0, 1.0);

		vt.turnDistance->addTerm(new fl::Ramp("SMALL", 0.6, 0.1));
		vt.turnDistance->addTerm(new fl::Trapezoid("MEDIUM", 0.1, 0.6, 0.8, 2.5));
		vt.turnDistance->addTerm(new fl::Ramp("LONG", 0.8, 2.5));
		vt.turnDistance->setRange(0.0, 3.0);

		vt.missionImportance->addTerm(new fl::Ramp("LOW", 2.5, 0));
		vt.missionImportance->addTerm(new fl::Triangle("MEDIUM", 2, 3));
		vt.missionImportance->addTerm(new fl::Ramp("HIGH", 2.5, 5));
		vt.missionImportance->setRange(0.0, 5.0);

		vt.goldReward->addTerm(new fl::Ramp("LOW", 500, 0));
		vt.goldReward->addTerm(new fl::Triangle("MEDIUM", 500, 1000, 3000));
		vt.goldReward->addTerm(new fl::Ramp("HIGH", 1000, 3000));
		vt.goldReward->setRange(0.0, 5000.0);

		vt.armyReward->addTerm(new fl::Ramp("LOW", 0.5, 0.2));
		vt.armyReward->addTerm(new fl::Triangle("MEDIUM", 0.2, 0.5, 1));
		vt.armyReward->addTerm(new fl::Ramp("HIGH", 0.5, 1));
		vt.armyReward->setRange(0.0, 1.0);

		vt.value->addTerm(new fl::Ramp("LOW", 0.4, 0.1));
		vt.value->addTerm(new fl::Triangle("MEDIUM", 0.4, 0.6));
		vt.value->addTerm(new fl::Ramp("HIGH", 0.6, 0.9));
		vt.value->setRange(0.0, 1.0);

		//use unarmed scouts if possible
		vt.addRule("if armyLoss is LOW then Value is MEDIUM");
		vt.addRule("if armyLoss is MEDIUM then Value is LOW");
		vt.addRule("if armyLoss is HIGH then Value is very LOW");
		//we may want to use secondary hero(es) rather than main hero

		//do not cancel important goals
		//vt.addRule("if lockedMissionImportance is HIGH then Value is very LOW");
		//vt.addRule("if lockedMissionImportance is MEDIUM then Value is somewhat LOW");
		//vt.addRule("if lockedMissionImportance is LOW then Value is HIGH");

		//pick nearby objects if it's easy, avoid long walks
		/*vt.addRule("if turnDistance is SMALL then Value is somewhat HIGH");
		vt.addRule("if turnDistance is MEDIUM then Value is MEDIUM");
		vt.addRule("if turnDistance is LONG then Value is LOW");*/

		//some goals are more rewarding by definition f.e. capturing town is more important than collecting resource - experimental
		vt.addRule("if goldReward is HIGH and turnDistance is not very LONG then Value is very HIGH");
		vt.addRule("if goldReward is HIGH and turnDistance is very LONG then Value is HIGH");
		vt.addRule("if goldReward is MEDIUM and turnDistance is MEDIUM then Value is somewhat HIGH");
		vt.addRule("if goldReward is MEDIUM and turnDistance is SMALL then Value is HIGH");
		vt.addRule("if goldReward is MEDIUM and turnDistance is LONG then Value is LOW");
		vt.addRule("if goldReward is LOW and turnDistance is very SMALL then Value is MEDIUM");
		vt.addRule("if goldReward is LOW and turnDistance is SMALL then Value is somewhat LOW");
		vt.addRule("if goldReward is LOW and turnDistance is MEDIUM then Value is LOW");
		vt.addRule("if goldReward is LOW and turnDistance is LONG then Value is very LOW");

		vt.addRule("if armyReward is HIGH and turnDistance is not very LONG then Value is very HIGH");
		vt.addRule("if armyReward is HIGH and turnDistance is very LONG then Value is HIGH");
		vt.addRule("if armyReward is MEDIUM and turnDistance is MEDIUM then Value is somewhat HIGH");
		vt.addRule("if armyReward is MEDIUM and turnDistance is SMALL then Value is HIGH");
		vt.addRule("if armyReward is MEDIUM and turnDistance is LONG then Value is LOW");
		vt.addRule("if armyReward is LOW and turnDistance is SMALL then Value is somewhat LOW");
		vt.addRule("if armyReward is LOW and turnDistance is MEDIUM then Value is LOW");
		vt.addRule("if armyReward is LOW and turnDistance is LONG then Value is very LOW");
	}
	catch(fl::Exception & fe)
	{
		logAi->error("visitTile: %s", fe.getWhat());
	}
}

int32_t estimateTownIncome(const CGObjectInstance * target, const CGHeroInstance * hero)
{
	auto relations = cb->getPlayerRelations(hero->tempOwner, target->tempOwner);

	if(relations != PlayerRelations::ENEMIES)
		return 0; // if we already own it, no additional reward will be received by just visiting it

	auto town = cb->getTown(target->id);
	auto isNeutral = target->tempOwner == PlayerColor::NEUTRAL;
	auto isProbablyDeveloped = !isNeutral && town->hasFort();

	return isProbablyDeveloped ? 1500 : 500;
}

TResources getCreatureBankResources(const CGObjectInstance * target, const CGHeroInstance * hero)
{
	auto objectInfo = VLC->objtypeh->getHandlerFor(target->ID, target->subID)->getObjectInfo(target->appearance);
	CBankInfo * bankInfo = dynamic_cast<CBankInfo *>(objectInfo.get());
	auto resources = bankInfo->getPossibleResourcesReward();

	return resources;
}

uint64_t getCreatureBankArmyReward(const CGObjectInstance * target, const CGHeroInstance * hero)
{
	auto objectInfo = VLC->objtypeh->getHandlerFor(target->ID, target->subID)->getObjectInfo(target->appearance);
	CBankInfo * bankInfo = dynamic_cast<CBankInfo *>(objectInfo.get());
	auto creatures = bankInfo->getPossibleCreaturesReward();
	uint64_t result = 0;

	for(auto c : creatures)
	{
		result += c.type->AIValue * c.count;
	}

	return result;
}

uint64_t getArmyReward(const CGObjectInstance * target, const CGHeroInstance * hero)
{
	const int dailyIncomeMultiplier = 5;

	switch(target->ID)
	{
	case Obj::TOWN:
		return target->tempOwner == PlayerColor::NEUTRAL ? 5000 : 1000;
	case Obj::CREATURE_BANK:
		return getCreatureBankArmyReward(target, hero);
	case Obj::CREATURE_GENERATOR1:
	case Obj::CREATURE_GENERATOR2:
	case Obj::CREATURE_GENERATOR3:
	case Obj::CREATURE_GENERATOR4:
		return 1500;
	case Obj::CRYPT:
	case Obj::SHIPWRECK:
	case Obj::SHIPWRECK_SURVIVOR:
		return 1500;
	case Obj::ARTIFACT:
		return dynamic_cast<const CArtifact *>(target)->getArtClassSerial() == CArtifact::ART_MAJOR ? 3000 : 1500;
	case Obj::DRAGON_UTOPIA:
		return 10000;
	default:
		return 0;
	}
}

/// Gets aproximated reward in gold. Daily income is multiplied by 5
int32_t getGoldReward(const CGObjectInstance * target, const CGHeroInstance * hero)
{
	const int dailyIncomeMultiplier = 5;
	auto isGold = target->subID == Res::GOLD; // TODO: other resorces could be sold but need to evaluate market power

	switch(target->ID)
	{
	case Obj::RESOURCE:
		return isGold ? 800 : 100;
	case Obj::TREASURE_CHEST:
		return 1500;
	case Obj::WATER_WHEEL:
		return 1000;
	case Obj::TOWN:
		return dailyIncomeMultiplier * estimateTownIncome(target, hero);
	case Obj::MINE:
	case Obj::ABANDONED_MINE:
		return dailyIncomeMultiplier * (isGold ? 1000 : 75);
	case Obj::MYSTICAL_GARDEN:
	case Obj::WINDMILL:
		return 200;
	case Obj::CAMPFIRE:
		return 900;
	case Obj::CREATURE_BANK:
		return getCreatureBankResources(target, hero)[Res::GOLD];
	case Obj::CRYPT:
	case Obj::DERELICT_SHIP:
		return 3000;
	case Obj::DRAGON_UTOPIA:
		return 10000;
	case Obj::SEA_CHEST:
		return 1500;
	default:
		return 0;
	}
}

/// distance
/// nearest hero?
/// gold income
/// army income
/// hero strength - hero skills
/// danger
/// importance
float FuzzyHelper::evaluate(Tasks::ExecuteChain * task, const CGHeroInstance * hero, const CGObjectInstance * target)
{
	double missionImportance = 0;
	double armyLossPersentage = task->armyLoss / (double)task->armyTotal;
	int32_t goldReward = getGoldReward(target, hero);
	uint64_t armyReward = getArmyReward(target, hero);
	double result = 0;

	try
	{
		vt.armyLossPersentage->setValue(armyLossPersentage);
		vt.heroStrength->setValue((fl::scalar)hero->getTotalStrength() / ai->primaryHero()->getTotalStrength());
		vt.turnDistance->setValue(task->turns);
		vt.goldReward->setValue(goldReward);
		vt.armyReward->setValue(armyReward / 10000.0);
		vt.missionImportance->setValue(missionImportance);

		vt.engine.process();
		//engine.process(VISIT_TILE); //TODO: Process only Visit_Tile
		result = vt.value->getValue();
	}
	catch(fl::Exception & fe)
	{
		logAi->error("evaluate VisitTile: %s", fe.getWhat());
	}
	assert(result >= 0);

	logAi->trace("Evaluated %s, loaa: %f, turns: %f, gold: %d, army gain: %d, result %f",
		task->toString(),
		armyLossPersentage,
		task->turns,
		goldReward,
		armyReward,
		result);

	return result;
}
float FuzzyHelper::evaluate(Goals::VisitHero & g)
{
	auto obj = cb->getObj(ObjectInstanceID(g.objid)); //we assume for now that these goals are similar
	if(!obj)
		return -100; //hero died in the meantime
	//TODO: consider direct copy (constructor?)
	g.setpriority(Goals::VisitTile(obj->visitablePos()).sethero(g.hero).setisAbstract(g.isAbstract).accept(this));
	return g.priority;
}

float FuzzyHelper::evaluate(Goals::BuildThis & g)
{
	return 1;
}
float FuzzyHelper::evaluate(Goals::DigAtTile & g)
{
	return 0;
}
float FuzzyHelper::evaluate(Goals::GetResources & g)
{
	return 0;
}
float FuzzyHelper::evaluate(Goals::Invalid & g)
{
	return -1e10;
}
float FuzzyHelper::evaluate(Goals::AbstractGoal & g)
{
	logAi->warn("Cannot evaluate goal %s", g.toString());
	return g.priority;
}
void FuzzyHelper::setPriority(Goals::TSubgoal & g)
{
	g->setpriority(g->accept(this)); //this enforces returned value is set
}
