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

#include "VisitTile.h"

struct HeroPtr;
class VCAI;
class FuzzyHelper;
struct SectorMap;

namespace Tasks
{
	class ExecuteChain : public TemplateTask<ExecuteChain> {
	private:
		std::string objInfo;
		std::string heroInfo;
		TaskList subTasks;
	public:
		int movementPointsUsed;
		double turns;
		int complexity;
		uint64_t armyLoss;
		uint64_t armyLeft;
		uint64_t armyTotal;

		ExecuteChain(
			const CHeroChainPath & chainPath,
			uint64_t totalArmy,
			uint64_t totalArmyLoss,
			const CGObjectInstance * obj = NULL);

		virtual void execute() override;
		virtual bool canExecute() override;
		virtual std::string toString() override;
	};
}