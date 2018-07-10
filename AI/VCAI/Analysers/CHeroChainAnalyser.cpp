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
#include "lib/CPathfinder.h"
#include "CHeroChainAnalyser.h"
#include "CHeroChainConfig.h"

extern boost::thread_specific_ptr<CCallback> cb;
extern boost::thread_specific_ptr<VCAI> ai;

void CHeroChainAnalyser::fill(TurnData * turnData){
	auto heroes = cb->getHeroesInfo();

	turnData->chainConfig->reset();

	for(auto hero : heroes)
	{
		turnData->chainConfig->addHero(hero);
	}
	
	turnData->chainInfo = cb->getHeroChainInfo(turnData->chainConfig);
}
