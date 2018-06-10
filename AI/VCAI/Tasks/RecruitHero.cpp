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
#include "../VCAI.h"
#include "lib/mapping/CMap.h" //for victory conditions
#include "lib/CPathfinder.h"
#include "RecruitHero.h"

extern boost::thread_specific_ptr<CCallback> cb;
extern boost::thread_specific_ptr<VCAI> ai;

using namespace Tasks;

void recruitHero(const CGTownInstance * t)
{
	logAi->debug("Trying to recruit a hero in %s at %s", t->name, t->visitablePos().toString());

	auto heroes = cb->getAvailableHeroes(t);
	if (heroes.size())
	{
		auto hero = heroes[0];
		if (heroes.size() >= 2) //makes sense to recruit two heroes with starting amries in first week
		{
			if (heroes[1]->getTotalStrength() > hero->getTotalStrength())
				hero = heroes[1];
		}
		cb->recruitHero(t, hero);
	}
}

void Tasks::RecruitHero::execute()
{
	recruitHero(targetTown);
	//TODO try to free way to blocked town
	//TODO: adventure map tavern or prison?
}

std::string RecruitHero::toString()
{
	return "RecruitHero";
}