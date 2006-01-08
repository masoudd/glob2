/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <Stream.h>

#include "AINicowar.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Player.h"
#include "Utilities.h"
#include "Unit.h"
#include "Ressource.h"
#include <algorithm>
#include <iterator>
#include "Brush.h"

using namespace std;
using namespace Nicowar;

AINicowar::AINicowar(Player *player)
{
	init(player);
}




AINicowar::AINicowar(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	bool goodLoad=load(stream, player, versionMinor);
	assert(goodLoad);
}




void AINicowar::init(Player *player)
{
	timer=0;
	iteration=0;
	center_x=0;
	center_y=0;
	module_timer=0;

	assert(player);

	attack_module=NULL;
	defense_module=NULL;
	new_construction_module=NULL;
	upgrade_repair_module=NULL;
	unit_module=NULL;

	new BasicDistributedSwarmManager(*this);
	new PrioritizedBuildingAttack(*this);
	new SimpleBuildingDefense(*this);
	new DistributedNewConstructionManager(*this);
	new RandomUpgradeRepairModule(*this);
	new ExplorationManager(*this);
	new InnManager(*this);
	new TowerController(*this);
	//new BuildingClearer(*this);
	//new HappinessHandler(*this);
	//new Farmer(*this);

	this->player=player;
	this->team=player->team;
	this->game=player->game;
	this->map=player->map;

	assert(this->team);
	assert(this->game);
	assert(this->map);
}




AINicowar::~AINicowar()
{
	for(vector<Module*>::iterator i=modules.begin(); i!=modules.end(); ++i)
	{
		if(*i)
			delete *i;
	}
}




bool AINicowar::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	init(player);

	stream->readEnterSection("AINicowar");
	timer=stream->readUint32("timer");
	iteration=stream->readUint32("iteration");
	center_x=stream->readUint32("center_x");
	center_y=stream->readUint32("center_y");
	module_timer=stream->readUint32("module_timer");
	active_module=modules.begin()+stream->readUint32("active_module");

	stream->readEnterSection("orders");
	Uint32 ordersSize = stream->readUint32("size");
	for (Uint32 ordersIndex = 0; ordersIndex < ordersSize; ordersIndex++)
	{
		stream->readEnterSection(ordersIndex);
		size_t size=stream->readUint32("size");
		Uint8* buffer = new Uint8[size];
		stream->read(buffer, size, "data");
		orders.push(Order::getOrder(buffer, size));
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	char signature[4];

	stream->readEnterSection("modules");
	Uint32 modulesSize = stream->readUint32("size");
	for (Uint32 modulesIndex = 0; modulesIndex < modulesSize; modulesIndex++)
	{
		stream->readEnterSection(modulesIndex);
		stream->read(signature, 4, "signatureStart");
		if (memcmp(signature,"MoSt", 4)!=0)
		{
			std::cout<<"Signature missmatch at begin of module #"<<modulesIndex<<", "<<modules[modulesIndex]->getName()<<". Expected \"MoEn\", recieved \""<<signature<<"\"."<<endl;
			stream->readLeaveSection();
			return false;
		}

		modules[modulesIndex]->load(stream, player, versionMinor);

		stream->read(signature, 4, "signatureEnd");
		if (memcmp(signature,"MoEn", 4)!=0)
		{
			std::cout<<"Signature missmatch at end of module #"<<modulesIndex<<", "<<modules[modulesIndex]->getName()<<". Expected \"MoEn\", recieved \""<<signature<<"\"."<<endl;
			stream->readLeaveSection();
			return false;
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readLeaveSection();
	return true;
}




void AINicowar::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("AINicowar");
	stream->writeUint32(timer, "timer");
	stream->writeUint32(iteration, "iteration");
	stream->writeUint32(center_x, "center_x");
	stream->writeUint32(center_y, "center_y");
	stream->writeUint32(module_timer, "module_timer");
	stream->writeUint32(active_module-modules.begin(), "active_module");

	stream->writeEnterSection("orders");
	stream->writeUint32((Uint32)orders.size(), "size");
	for (Uint32 ordersIndex = 0; ordersIndex < orders.size(); ordersIndex++)
	{
		stream->writeEnterSection(ordersIndex);
		Order* order = orders.front();
		orders.pop();
		stream->writeUint32(order->getDataLength()+1, "size");
		stream->write(order->getData(), order->getDataLength(), "data");
		delete order;
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("modules");
	stream->writeUint32(modules.size(), "size");
	for(vector<Module*>::iterator i = modules.begin(); i!=modules.end(); ++i)
	{
		stream->write("MoSt", 4, "signatureStart");
		(*i)->save(stream);
		stream->write("MoEn", 4, "signatureEnd");
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




Order *AINicowar::getOrder(void)
{

	//See if there is an existing order that the AI wanted to have done
	if (!orders.empty())
	{
		Order* order = orders.front();
		orders.pop();
		return order;
	}

	timer++;

	//Waits for atleast one iteration before it does anything, because of a few odd, 'goes to fast' bugs.
	if (timer<STARTUP_TIME)
		return new NullOrder();

	//	std::cout<<"timer="<<timer<<";"<<endl;

	if(active_module==modules.end() || iteration==0)
	{
		if(iteration==0)
		{
			setCenter();
			if(SEE_EVERYTHING)
			{
				for(int x=0; x<map->getW(); ++x)
				{
					for(int y=0; y<map->getH(); ++y)
					{
						map->setMapDiscovered(x, y, team->me);
					}
				}
			}
		}
		iteration+=1;
		active_module=modules.begin();
		if(AINicowar_DEBUG)
			std::cout<<"AINicowar: getOrder: ******Entering iteration "<<iteration<<" at tick #"<<timer<<". ******"<<endl;
	}

	//The +1 here is because the program would end one tick late, so the tick #'s that
	//it entered each iteration at would be 171, or 441 (for an interval of ten).
	//This -1 corrects that.
	if((timer+1)%TIMER_INTERVAL==0)
	{
		//		std::cout<<"Performing function: timer="<<timer<<"; module_timer="<<module_timer<<";"<<endl;
		bool cont = (*active_module)->perform(module_timer);
		if(!cont)
			++module_timer;

		if(module_timer==(*active_module)->numberOfTicks())
		{
			module_timer=0;
			++active_module;
		}
	};

	if (!orders.empty())
	{
		Order* order = orders.front();
		orders.pop();
		return order;
	}

	return new NullOrder();
}




void AINicowar::setDefenseModule(DefenseModule* module)
{
	remove(modules.begin(), modules.end(), defense_module);
	if(defense_module!=NULL)
		delete defense_module;
	defense_module=module;
	modules.push_back(module);
}




void AINicowar::setAttackModule(AttackModule* module)
{
	remove(modules.begin(), modules.end(), attack_module);
	if(attack_module!=NULL)
		delete attack_module;
	attack_module=module;
	modules.push_back(module);
}




void AINicowar::setNewConstructionModule(NewConstructionModule* module)
{
	remove(modules.begin(), modules.end(), new_construction_module);
	if(new_construction_module!=NULL)
		delete new_construction_module;
	new_construction_module=module;
	modules.push_back(module);
}




void AINicowar::setUpgradeRepairModule(UpgradeRepairModule* module)
{
	remove(modules.begin(), modules.end(), upgrade_repair_module);

	if(upgrade_repair_module!=NULL)
		delete upgrade_repair_module;
	upgrade_repair_module=module;
	modules.push_back(module);
}




void AINicowar::setUnitModule(UnitModule* module)
{
	remove(modules.begin(), modules.end(), unit_module);
	if(unit_module!=NULL)
		delete unit_module;
	unit_module=module;
	modules.push_back(module);
}




void AINicowar::addOtherModule(OtherModule* module)
{
	other_modules[module->getName()]=module;
	modules.push_back(module);
}




DefenseModule* AINicowar::getDefenseModule()
{
	return defense_module;
}




AttackModule* AINicowar::getAttackModule()
{
	return attack_module;
}




NewConstructionModule* AINicowar::getNewConstructionModule()
{
	return new_construction_module;
}




UpgradeRepairModule* AINicowar::getUpgradeRepairModule()
{
	return upgrade_repair_module;
}




UnitModule* AINicowar::getUnitModule()
{
	return unit_module;
}




OtherModule* AINicowar::getOtherModule(string name)
{
	return other_modules[name];
}




void AINicowar::setCenter()
{
	unsigned int x_total=0;
	unsigned int y_total=0;
	unsigned int square_total=0;
	for(int x=0; x < map->getW(); ++x)
	{
		for(int y=0; y<map->getH(); ++y)
		{
			if(map->isMapDiscovered(x, y, team->me))
			{
				x_total+=x;
				y_total+=y;
				square_total++;
			}
		}
	}
	center_x=x_total/square_total;
	center_y=y_total/square_total;
}




bool Nicowar::buildingStillExists(Game* game, Building* building)
{
	for (int i=0; i<32; i++)
	{
		Team* t = game->teams[i];
		if(t)
		{
			for(int i=0; i<1024; i++)
			{
				Building* b = t->myBuildings[i];
				if (b)
				{
					if(b == building)
						return true;
				}
			}
		}
	}
	return false;
}




bool Nicowar::buildingStillExists(Game* game, unsigned int gid)
{
	return getBuildingFromGid(game, gid)!=NULL;
}




GridPollingSystem::GridPollingSystem(AINicowar& ai)
{
	map=ai.map;
	team=ai.team;
	game=ai.game;
	center_x=ai.getCenterX();
	center_y=ai.getCenterY();
}




unsigned int GridPollingSystem::pollArea(unsigned int x, unsigned int y, unsigned int width, unsigned int height, pollModifier mod, pollType poll_type)
{
	unsigned int bound_h=x+width;
	if(static_cast<int>(bound_h)>map->getW())
		bound_h-=map->getW();

	unsigned int bound_y=y+height;
	if(static_cast<int>(bound_y)>map->getH())
		bound_y-=map->getH();

	unsigned int orig_y=y;
	unsigned int score=0;

	//This is an optmization, as putting the switch inside the for loop causes it to do log2n checks
	//For every single square, which has become to cumbersome.
	Unit* u=NULL;
	Building* b=NULL;
	switch (poll_type)
	{

		case HIDDEN_SQUARES:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->getW())
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->getH())
						y=0;
					if(!map->isMapDiscovered(x, y, team->me))
					{
						score++;
					}
				}
			}

			break;
		case VISIBLE_SQUARES:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->getW())
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->getH())
						y=0;
					if(map->isMapDiscovered(x, y, team->me))
					{
						score++;
					}
				}
			}
			break;
		case ENEMY_BUILDINGS:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->getW())
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->getH())
						y=0;
					b = getBuildingFromGid(game, map->getBuilding(x, y));
					if (b)
					{
						if((b->owner->me & team->enemies) && b->posX == static_cast<int>(x) && b->posY == static_cast<int>(y))
						{
							score++;
						}
					}
				}
			}
			break;
		case FRIENDLY_BUILDINGS:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->getW())
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->getH())
						y=0;
					b = getBuildingFromGid(game, map->getBuilding(x, y));
					if (b)
					{
						if(b->owner->me == team->me && b->posX == static_cast<int>(x) && b->posY == static_cast<int>(y))
						{
							score++;
						}
					}
				}
			}
			break;
		case ENEMY_UNITS:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->getW())
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->getH())
						y=0;
					u = getUnitFromGid(game, map->getGroundUnit(x, y));
					if (u)
					{
						if((u->owner->me & team->enemies) && u->posX==static_cast<int>(x) && u->posY == static_cast<int>(y))
						{
							score++;
						}
					}
				}
			}
			break;
		case ENEMY_WARRIORS:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->getW())
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->getH())
						y=0;
					u = getUnitFromGid(game, map->getGroundUnit(x, y));
					if (u)
					{
						if((u->owner->me & team->enemies) && u->posX==static_cast<int>(x) && u->posY == static_cast<int>(y) && u->typeNum==WARRIOR)
						{
							score++;
						}
					}
				}
			}
			break;
		case POLL_CORN:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->getW())
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->getH())
						y=0;
					if (map->isRessourceTakeable(x, y, CORN))
						score++;
				}
			}
		case POLL_TREES:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->getW())
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->getH())
						y=0;
					if (map->isRessourceTakeable(x, y, WOOD))
						score++;
				}
			}
			break;
		case POLL_STONE:
			for(; x!=bound_h; ++x)
			{
				if(static_cast<int>(x) >= map->getW())
					x=0;
				for(y=orig_y; y!=bound_y; ++y)
				{
					if(static_cast<int>(y) >= map->getH())
						y=0;
					if (map->isRessourceTakeable(x, y, STONE))
						score++;
				}
			}
			break;
		case CENTER_DISTANCE:
			if(mod==MAXIMUM)
				score=intdistance((x+width), center_x) + intdistance((y+height), center_y);
			if(mod==MINIMUM)
				score=(map->getW()-intdistance((x+width), center_x)) + (map->getH()-intdistance((y+height), center_y));
			return score;
			break;
		case NONE:
			return 0;
			break;
	}

	switch (mod)
	{
		case MAXIMUM:
			return score;
			break;
		case MINIMUM:
			return width*height-score;
			break;
	}

	return 0;
}




GridPollingSystem::zone GridPollingSystem::getZone(unsigned int x, unsigned int y, unsigned int area_width, unsigned int area_height, int horizontal_overlap, int vertical_overlap)
{
	int free_width=area_width-horizontal_overlap;
	int free_height=area_height-vertical_overlap;
	zone z;
	z.x = x-(x%free_width);
	z.y = y-(x%free_height);
	z.width=area_width;
	z.height=area_height;
	return z;
}




int GridPollingSystem::getPositionScore(const std::vector<pollRecord>& polls, const std::vector<pollRecord>::const_iterator& iter)
{
	int score=0;
	std::vector<pollRecord>::const_iterator last = polls.begin();
	for(std::vector<pollRecord>::const_iterator i = polls.begin(); i!=polls.end(); ++i)
	{
		if(i==iter)
		{
			break;
		}
		if(i->score != last->score)
		{
			score++;
			last=i;
		}
	}
	return score;
}




GridPollingSystem::getBestZonesSplit* GridPollingSystem::getBestZones(poll p, unsigned int width, unsigned int height, int horizontal_overlap, int vertical_overlap, unsigned int extention_width, unsigned int extention_height)
{
	getBestZonesSplit* split=new getBestZonesSplit;
	split->p=p;
	split->width=width;
	split->height=height;
	split->horizontal_overlap=horizontal_overlap;
	split->vertical_overlap=vertical_overlap;
	split->extention_width=extention_width;
	split->extention_height=extention_height;
	std::vector<pollRecord>& a_list=*new std::vector<pollRecord>;
	std::vector<pollRecord>& b_list=*new std::vector<pollRecord>;
	std::vector<pollRecord>& c_list=*new std::vector<pollRecord>;
	split->a_list=&a_list;
	split->b_list=&b_list;
	split->c_list=&c_list;

	for (unsigned int x=0; x<=static_cast<unsigned int>(map->getW()-horizontal_overlap); x+=width-horizontal_overlap)
	{
		for (unsigned int y=0; y<=static_cast<unsigned int>(map->getH()-vertical_overlap); y+=height-vertical_overlap)
		{
			int full_x=x-extention_width;
			if(full_x<0)
				full_x=map->getW()+full_x;
			int full_y=y-extention_height;
			if(full_y<0)
				full_y=map->getH()+full_y;

			int full_w=width+2*extention_width;
			int full_h=height+2*extention_height;

			pollRecord pr_a;
			pr_a.failed_constraint=false;
			if(pollArea(full_x, full_y, full_w, full_h, p.mod_minimum, p.minimum_type)<p.minimum_score)
				if(p.is_strict_minimum)
					continue;
				else
					pr_a.failed_constraint=true;

			if(pollArea(full_x, full_y, full_w, full_h, p.mod_maximum, p.maximum_type)>p.maximum_score)
				if(p.is_strict_minimum)
					continue;
				else
					pr_a.failed_constraint=true;

			pr_a.x=x;
			pr_a.y=y;
			pr_a.width=width;
			pr_a.height=height;
			pollRecord pr_b=pr_a;
			pollRecord pr_c=pr_b;
			pr_a.score=pollArea(full_x, full_y, full_w, full_h, p.mod_1, p.type_1);
			pr_b.score=pollArea(full_x, full_y, full_w, full_h, p.mod_2, p.type_2);
			pr_c.score=pollArea(full_x, full_y, full_w, full_h, p.mod_3, p.type_3);
			a_list.push_back(pr_a);
			b_list.push_back(pr_b);
			c_list.push_back(pr_c);
		}
	}

	std::sort(a_list.begin(), a_list.end());
	std::sort(b_list.begin(), b_list.end());
	std::sort(c_list.begin(), c_list.end());
	return split;
}




TeamStatsGenerator::TeamStatsGenerator(Team* team) : team(team)
{

}




std::vector<GridPollingSystem::zone> GridPollingSystem::getBestZones(GridPollingSystem::getBestZonesSplit* split_calc)
{
	std::vector<pollRecord>& a_list=*split_calc->a_list;
	std::vector<pollRecord>& b_list=*split_calc->b_list;
	std::vector<pollRecord>& c_list=*split_calc->c_list;

	std::vector<threeTierRecord> final;
	unsigned int pos_a=0;
	for (std::vector<pollRecord>::iterator i1=a_list.begin(); i1!=a_list.end(); ++i1)
	{
		pos_a++;

		threeTierRecord ttr;
		ttr.x=i1->x;
		ttr.y=i1->y;
		ttr.width=i1->width;
		ttr.height=i1->height;

		for(std::vector<pollRecord>::iterator s1=i1; s1!=a_list.end() && s1->score == i1->score; ++s1)
			pos_a++;

		ttr.score_a=pos_a;

		unsigned int pos_b=0;
		for (std::vector<pollRecord>::iterator i2=b_list.begin(); i2!=b_list.end(); ++i2)
		{
			pos_b++;
			if(i2->x==ttr.x && i2->y==ttr.y)
			{
				for(std::vector<pollRecord>::iterator s2=i2; s2!=b_list.end() && s2->score == i2->score; ++s2)
					pos_b++;
				ttr.score_b=pos_b;
				break;
			}
		}

		unsigned int pos_c=0;
		for (std::vector<pollRecord>::iterator i3=c_list.begin(); i3!=c_list.end(); ++i3)
		{
			pos_c++;
			if(i3->x==ttr.x && i3->y==ttr.y)
			{
				for(std::vector<pollRecord>::iterator s3=i3; s3!=c_list.end() && s3->score == i3->score; ++s3)
					pos_c++;
				ttr.score_c=pos_c;
				break;
			}
		}
		final.push_back(ttr);
	}

	std::sort(final.begin(), final.end());

	std::vector<zone> return_list;
	//For some reason, the final list is being sorted backwards. It seems to be because of my lack of thoroughly thinking out
	//how the various comparisons relate.
	for(std::vector<threeTierRecord>::reverse_iterator i=final.rbegin(); i!=final.rend(); ++i)
	{
		zone z;
		z.x=i->x;
		z.y=i->y;
		z.width=i->width;
		z.height=i->height;
		return_list.push_back(z);
	}

	delete split_calc->a_list;
	delete split_calc->b_list;
	delete split_calc->c_list;
	delete split_calc;

	return return_list;
}




unsigned int TeamStatsGenerator::getUnits(unsigned int type, Unit::Medical medical_state, Unit::Activity activity, unsigned int ability, unsigned int level, bool isMinimum)
{
	//This is because ability levels are counted from 0, not 1. I'm not sure why,
	//though, because 0 would mean that the unit does not have that skill, which
	//would be more appropriette.
	level-=1;
	unsigned int free_workers=0;
	Unit **myUnits=team->myUnits;

	for (int i=0; i<1024; i++)
	{
		Unit* u = myUnits[i];
		if (u)
		{
			if (u->typeNum == static_cast<int>(type) && u->activity==activity && ((!isMinimum && u->level[ability]==static_cast<int>(level)) ||
				(isMinimum && u->level[ability]>=static_cast<int>(level))) && u->medical==medical_state)
			{
				free_workers+=1;
			}
		}
	}
	return free_workers;
}




unsigned int TeamStatsGenerator::getMaximumBuildingLevel(unsigned int building_type)
{
	unsigned int level=0;
	for (int i=0; i<1024; ++i)
	{
		Building* b = team->myBuildings[i];
		if(b)
		{
			if(b->type->shortTypeNum == static_cast<int>(building_type))
			{
				level=std::max(static_cast<int>(level), b->type->level+1);
			}
		}
	}
	return level;
}




SimpleBuildingDefense::SimpleBuildingDefense(AINicowar& ai) : ai(ai)
{
	ai.setDefenseModule(this);
}




bool SimpleBuildingDefense::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return findDefense();
		case 1:
			return updateFlags();
		case 2:
			return findCreatedDefenseFlags();
	}
	return false;
}




string SimpleBuildingDefense::getName() const
{
	return "SimpleBuildingDefense";
}




bool SimpleBuildingDefense::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{

	stream->readEnterSection("SimpleBuildingDefense");
	stream->readEnterSection("defending_zones");
	Uint32 defenseRecordSize = stream->readUint32("size");
	for (Uint32 defenseRecordIndex = 0; defenseRecordIndex < defenseRecordSize; defenseRecordIndex++)
	{
		stream->readEnterSection(defenseRecordIndex);
		defenseRecord dr;
		dr.flag=stream->readUint32("flag");
		dr.flagx=stream->readUint32("flagx");
		dr.flagy=stream->readUint32("flagy");
		dr.zonex=stream->readUint32("zonex");
		dr.zoney=stream->readUint32("zoney");
		dr.width=stream->readUint32("width");
		dr.height=stream->readUint32("height");
		dr.assigned=stream->readUint32("assigned");
		dr.building=stream->readUint32("building");
		defending_zones.push_back(dr);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void SimpleBuildingDefense::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("SimpleBuildingDefense");
	stream->writeEnterSection("defending_zones");
	stream->writeUint32(defending_zones.size(), "size");
	for (Uint32 defenseRecordIndex = 0; defenseRecordIndex < defending_zones.size(); defenseRecordIndex++)
	{
		stream->writeEnterSection(defenseRecordIndex);
		std::vector<defenseRecord>::const_iterator i = defending_zones.begin() + defenseRecordIndex;
		stream->writeUint32(i->flag, "flag");
		stream->writeUint32(i->flagx, "flagx");
		stream->writeUint32(i->flagy, "flagy");
		stream->writeUint32(i->zonex, "zonex");
		stream->writeUint32(i->zoney, "zoney");
		stream->writeUint32(i->width, "width");
		stream->writeUint32(i->height, "height");
		stream->writeUint32(i->assigned, "assigned");
		stream->writeUint32(i->building, "building");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




bool SimpleBuildingDefense::findDefense()
{
	ai.getUnitModule()->changeUnits("SimpleBuildingDefense", WARRIOR, 0, ATTACK_STRENGTH, 1, UnitModule::low);
	GridPollingSystem gps(ai);
	for(map<unsigned int, unsigned int>::iterator i=building_health.begin(); i!=building_health.end(); ++i)
	{
		Building* b = getBuildingFromGid(ai.game, i->first);
		if(b && b->type->shortTypeNum!=IntBuildingType::EXPLORATION_FLAG &&
			b->type->shortTypeNum!=IntBuildingType::WAR_FLAG &&
			b->type->shortTypeNum!=IntBuildingType::CLEARING_FLAG)
		{
			if(building_health.find(b->gid) != building_health.end())
			{
				if(b->hp < static_cast<int>(building_health[b->gid]))
				{
					defenseRecord dr;
					dr.flag=NOGBID;
					dr.flagx=b->posX+(b->type->width/2);
					dr.flagy=b->posY+(b->type->height/2);

					bool found=false;
					for(vector<defenseRecord>::iterator j = defending_zones.begin(); j != defending_zones.end(); ++j)
					{
						if(b->gid == j->building)
						{
							found=true;
							break;
						}
					}
					if(found)
					{
						continue;
					}
					dr.zonex=b->posX-DEFENSE_ZONE_BUILDING_PADDING;
					dr.zoney=b->posY-DEFENSE_ZONE_BUILDING_PADDING;
					dr.width=b->type->width+DEFENSE_ZONE_BUILDING_PADDING*2;
					dr.height=b->type->height+DEFENSE_ZONE_BUILDING_PADDING*2;
					dr.assigned=std::min(20u, gps.pollArea(dr.zonex, dr.zoney, dr.width, dr.height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_WARRIORS));
					dr.building=b->gid;
					defending_zones.push_back(dr);
					Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
					if(AINicowar_DEBUG)
						std::cout<<"AINicowar: findDefense: Creating a defense flag at "<<dr.flagx<<", "<<dr.flagy<<", to combat "<<dr.assigned<<" units that are attacking our "<<IntBuildingType::reverseConversionMap[b->type->shortTypeNum]<<" at "<<b->posX<<","<<b->posY<<"."<<endl;
					ai.orders.push(new OrderCreate(ai.team->teamNumber, dr.flagx, dr.flagy, typeNum));
				}
			}
		}
		else
		{
			building_health.erase(i);
		}
	}

	for(unsigned int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
		if(b)
		{
			if(b->type->shortTypeNum!=IntBuildingType::EXPLORATION_FLAG &&
				b->type->shortTypeNum!=IntBuildingType::WAR_FLAG &&
				b->type->shortTypeNum!=IntBuildingType::CLEARING_FLAG)
			{
				building_health[b->gid]=std::min(b->type->hpMax, b->hp);
			}
		}
	}
	return false;
}




bool SimpleBuildingDefense::updateFlags()
{
	GridPollingSystem gps(ai);
	for (vector<defenseRecord>::iterator i=defending_zones.begin(); i!=defending_zones.end();)
	{
		if(i->flag!=NOGBID)
		{
			unsigned int score = gps.pollArea(i->zonex, i->zoney, i->width, i->height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_WARRIORS);
			if(score==0)
			{
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: updateFlags: Found a flag at "<<i->flagx<<","<<i->flagy<<" that is no longer defending against any enemy units. Removing this flag."<<endl;
				ai.orders.push(new OrderDelete(i->flag));
				i=defending_zones.erase(i);
				continue;
			}
			else
			{
				if(static_cast<int>(score)!=getBuildingFromGid(ai.game, i->flag)->maxUnitWorking)
				{
					ai.orders.push(new OrderModifyBuilding(i->flag, score));
					ai.getUnitModule()->request("SimpleBuildingDefense", WARRIOR, ATTACK_STRENGTH, 1, std::min(20u, score), UnitModule::high, i->flag);
				}
			}
		}
		++i;
	}
	return false;
}




bool SimpleBuildingDefense::findCreatedDefenseFlags()
{
	for(unsigned int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
		if(b)
		{
			if(b->type->shortTypeNum == IntBuildingType::WAR_FLAG)
			{
				for (vector<defenseRecord>::iterator i=defending_zones.begin(); i!=defending_zones.end(); ++i)
				{
					if(i->flag == NOGBID && b->posX == static_cast<int>(i->flagx) && b->posY == static_cast<int>(i->flagy))
					{
						if(AINicowar_DEBUG)
							std::cout<<"AINicowar: findCreatedDefenseFlags: Found created flag at "<<i->flagx<<","<<i->flagy<<", adding it to the defense records."<<endl;
						i->flag=b->gid;
						ai.orders.push(new OrderModifyFlag(b->gid, std::max(i->width, i->height)/2));
						ai.orders.push(new OrderModifyBuilding(b->gid, i->assigned));
					ai.getUnitModule()->request("PrioritizedBuildingAttack", WARRIOR, ATTACK_STRENGTH, 1, i->assigned, UnitModule::high, i->flag);
						break;
					}
				}
			}
		}
	}
	return false;
}




GeneralsDefense::GeneralsDefense(AINicowar& ai) : ai(ai)
{
	ai.setDefenseModule(this);
}




bool GeneralsDefense::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return findEnemyFlags();
		case 1:
			return updateDefenseFlags();
	}
	return false;
}




string GeneralsDefense::getName() const
{
	return "GeneralsDefense";
}




bool GeneralsDefense::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("GeneralsDefense");
	stream->readEnterSection("defending_flags");
	Uint32 defenseRecordSize = stream->readUint32("size");
	for (Uint32 defenseRecordIndex = 0; defenseRecordIndex < defenseRecordSize ; defenseRecordIndex++)
	{
		stream->readEnterSection(defenseRecordIndex);
		defenseRecord dr;
		dr.flag=stream->readUint32("flag");
		dr.enemy_flag=stream->readUint32("enemy_flag");
		defending_flags.push_back(dr);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void GeneralsDefense::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("GeneralsDefense");
	stream->writeEnterSection("defending_flags");
	stream->writeUint32(defending_flags.size(), "size");
	for (Uint32 defenseRecordIndex = 0; defenseRecordIndex < defending_flags.size(); defenseRecordIndex++)
	{
		stream->writeEnterSection(defenseRecordIndex);
		std::vector<defenseRecord>::const_iterator i = defending_flags.begin() + defenseRecordIndex;
		stream->writeUint32(i->flag, "flag");
		stream->writeUint32(i->enemy_flag, "enemy_flag");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




bool GeneralsDefense::findEnemyFlags()
{
	GridPollingSystem gps(ai);
	for (unsigned int t=0; t<32; t++)
	{
		Team* team = ai.game->teams[t];
		if(team)
		{
			if(team->me & ai.team->enemies)
			{
				for(unsigned int n=0; n<1024; ++n)
				{
					Building* b = team->myBuildings[n];
					if(b)
					{
						if(b->type->shortTypeNum==IntBuildingType::WAR_FLAG)
						{

							unsigned int score = gps.pollArea((b->posX)-(b->unitStayRange)-DEFENSE_ZONE_SIZE_INCREASE,
								(b->posY)-(b->unitStayRange)-DEFENSE_ZONE_SIZE_INCREASE,
								b->unitStayRange*2+DEFENSE_ZONE_SIZE_INCREASE*2,
								b->unitStayRange*2+DEFENSE_ZONE_SIZE_INCREASE*2, GridPollingSystem::MAXIMUM,
								GridPollingSystem::FRIENDLY_BUILDINGS);

							if(score>0)
							{
								bool found=false;
								for(vector<defenseRecord>::iterator i = defending_flags.begin(); i!= defending_flags.end(); ++i)
								{
									if(i->enemy_flag == b->gid)
										found=true;
								}
								if(found)
									continue;

								defenseRecord dr;
								dr.flag=NOGBID;
								dr.enemy_flag=b->gid;
								defending_flags.push_back(dr);
								if(AINicowar_DEBUG)
									std::cout<<"AINicowar: findEnemyFlags: Creating new flag at "<<b->posX<<","<<b->posY<<" to combat an enemy attack!"<<endl;
								Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
								ai.orders.push(new OrderCreate(ai.team->teamNumber, b->posX, b->posY, typeNum));
							}
						}
					}
				}
			}
		}
	}
	return false;
}




bool GeneralsDefense::updateDefenseFlags()
{
	for(vector<defenseRecord>::iterator i = defending_flags.begin(); i!= defending_flags.end();)
	{
		if(buildingStillExists(ai.game, i->enemy_flag)==false)
		{
			i=defending_flags.erase(i);
			continue;
		}

		if(i->flag==NOGBID)

		{
			Building* eb = getBuildingFromGid(ai.game, i->enemy_flag);
			for(unsigned int n=0; n<1024; ++n)
			{
				Building* b = ai.team->myBuildings[n];
				if(b)
				{
					if(b->type->shortTypeNum == IntBuildingType::WAR_FLAG && b->posX==eb->posX && b->posY==eb->posY)
					{
						i->flag=b->gid;
						ai.orders.push(new OrderModifyFlag(i->flag, eb->unitStayRange));
						ai.orders.push(new OrderModifyBuilding(i->flag, eb->maxUnitWorking));
						ai.orders.push(new OrderModifyMinLevelToFlag(i->flag, eb->minLevelToFlag));
						break;
					}
				}
			}
		}
		++i;
	}
	return false;
}




PrioritizedBuildingAttack::PrioritizedBuildingAttack(AINicowar& ai) : ai(ai)
{
	ai.setAttackModule(this);
	enemy=NULL;
}




bool PrioritizedBuildingAttack::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return targetEnemy();
		case 1:
			return updateAttackFlags();
		case 2:
			return attack();
		case 3:
			return updateAttackFlags();
	}
	return false;
}




string PrioritizedBuildingAttack::getName() const
{
	return "PrioritizedBuildingAttack";
}




bool PrioritizedBuildingAttack::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("PrioritizedBuildingAttack");
	stream->readEnterSection("attacks");
	enemy = ai.game->teams[stream->readUint8("teamNumber")];
	Uint32 attackRecordSize = stream->readUint32("size");
	for (Uint32 attackRecordIndex = 0; attackRecordIndex < attackRecordSize; attackRecordIndex++)
	{
		stream->readEnterSection(attackRecordIndex);
		attackRecord ar;
		ar.target=stream->readUint32("target");
		ar.target_x=stream->readUint32("target_x");
		ar.target_y=stream->readUint32("target_y");
		ar.flag=stream->readUint32("flag");
		ar.flagx=stream->readUint32("flagx");
		ar.flagy=stream->readUint32("flagy");
		ar.zonex=stream->readUint32("zonex");
		ar.zoney=stream->readUint32("zoney");
		ar.width=stream->readUint32("width");
		ar.height=stream->readUint32("height");
		ar.unitx=stream->readUint32("unitx");
		ar.unity=stream->readUint32("unity");
		ar.unit_width=stream->readUint32("unit_width");
		ar.unit_height=stream->readUint32("unit_height");
		ar.assigned_units=stream->readUint32("assigned_units");
		ar.assigned_level=stream->readUint32("assigned_level");
		attacks.push_back(ar);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void PrioritizedBuildingAttack::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("PrioritizedBuildingAttack");
	stream->writeEnterSection("attacks");
	stream->writeUint8(enemy->teamNumber, "teamNumber");
	stream->writeUint32(attacks.size(), "size");
	for (Uint32 attackRecordIndex = 0; attackRecordIndex < attacks.size(); attackRecordIndex++)
	{
		stream->writeEnterSection(attackRecordIndex);
		std::vector<attackRecord>::const_iterator i = attacks.begin() + attackRecordIndex;
		stream->writeUint32(i->target, "target");
		stream->writeUint32(i->target_x, "target_x");
		stream->writeUint32(i->target_y, "target_y");
		stream->writeUint32(i->flag, "flag");
		stream->writeUint32(i->flagx, "flagx");
		stream->writeUint32(i->flagy, "flagy");
		stream->writeUint32(i->zonex, "zonex");
		stream->writeUint32(i->zoney, "zoney");
		stream->writeUint32(i->width, "width");
		stream->writeUint32(i->height, "height");
		stream->writeUint32(i->unitx, "unitx");
		stream->writeUint32(i->unity, "unity");
		stream->writeUint32(i->unit_width, "unit_width");
		stream->writeUint32(i->unit_height, "unit_height");
		stream->writeUint32(i->assigned_units, "assigned_units");
		stream->writeUint32(i->assigned_level, "assigned_level");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




bool PrioritizedBuildingAttack::targetEnemy()
{
	if(enemy==NULL || !enemy->isAlive)
	{
		for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end();)
		{
			if(j->flag!=NOGBID)
			{
				ai.orders.push(new OrderDelete(j->flag));
				j=attacks.erase(j);
			}
			else
			{
				++j;
			}
		}

		vector<Team*> targets;
		for(int i=0; i<32; ++i)
		{
			Team* t = ai.game->teams[i];
			if(t)
			{
				if((t->me & ai.team->enemies) && t->isAlive)
				{
					targets.push_back(t);
				}
			}
		}

		if(targets.size()>0)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: targetEnemy: A new enemy has been chosen."<<endl;
			enemy=targets[syncRand()%targets.size()];
		}
	}
	return false;
}




bool PrioritizedBuildingAttack::attack()
{
	GridPollingSystem gps(ai);

	//The following gets the highest barracks level the player has
	unsigned int max_barracks_level=0;
	unsigned int found_barracks=0;
	for(int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
		if(b)
		{
			if(b->type->shortTypeNum==IntBuildingType::ATTACK_BUILDING)
			{
				if(b->constructionResultState==Building::NO_CONSTRUCTION)
				{
					max_barracks_level=std::max(max_barracks_level, static_cast<unsigned int>(b->type->level+1));
					found_barracks++;
				}
			}
		}
	}
	///The average strength level of the teams warriors
	unsigned int average_unit_strength_level=0;
	unsigned int total=0;
	for(unsigned int i=0; static_cast<int>(i)<NB_UNIT_LEVELS; ++i)
	{
		if(i<MINIMUM_BARRACKS_LEVEL+1)
			continue;
		total+=ai.team->stats.getLatestStat()->upgradeState[ATTACK_STRENGTH][i];
		average_unit_strength_level+=ai.team->stats.getLatestStat()->upgradeState[ATTACK_STRENGTH][i]*(i+1);
	}
	if(total>0)
		average_unit_strength_level=static_cast<unsigned int>(std::floor(static_cast<float>(average_unit_strength_level)/static_cast<float>(total)+0.5))-1;


	unsigned int strength_level=USE_MAX_BARRACKS_LEVEL ? max_barracks_level : average_unit_strength_level;

	//If we don't have enough barracks, don't bother doing anything, otherwise, make sure where producing warriors.
	if(max_barracks_level<MINIMUM_BARRACKS_LEVEL+1 || found_barracks==0)
		return false;
	else
	{
		for(int i=0; i<NB_UNIT_LEVELS; ++i)
		{
			ai.getUnitModule()->changeUnits("PrioritizedBuildingAttack", WARRIOR, 0, ATTACK_STRENGTH, i+1, UnitModule::medium);
		}
		unsigned int numWarriors=ai.team->stats.getLatestStat()->numberUnitPerType[WARRIOR];
		ai.getUnitModule()->changeUnits("PrioritizedBuildingAttack", WARRIOR,
			std::min(BASE_ATTACK_WARRIORS, round_up(numWarriors, WARRIOR_DEVELOPMENT_CHUNK_SIZE)+WARRIOR_DEVElOPMENT_CONSISTANT_SIZE),
			ATTACK_STRENGTH, strength_level+1, UnitModule::medium);
	}

	//Check if we have enough units of the right level
	unsigned int available_units = ai.getUnitModule()->available("PrioritizedBuildingAttack", WARRIOR, ATTACK_STRENGTH, strength_level+1, true, UnitModule::medium);
	if(available_units<MINIMUM_TO_ATTACK)
		return false;

	//Don't use units that are needed by other flags
	for(vector<attackRecord>::iterator i = attacks.begin(); i!=attacks.end(); ++i)
	{
		if(i->assigned_level == strength_level)
		{
			unsigned int needed=0;
			if(i->flag!=NOGBID)
			{
				Building* b = getBuildingFromGid(ai.game, i->flag);
				needed=i->assigned_units-b->unitsWorking.size();
			}
			else
			{
				needed=i->assigned_units;
			};
			if(available_units > needed)
				available_units-=needed;
			else if(available_units <= needed)
				available_units=0;
		}
	}

	//The following goes through each of the buildings in the enemies foothold, and adds them to the appropriette list based on their position in the
	//ATTACK_PRIORITY variable.
	vector<vector<Building*> > buildings(IntBuildingType::NB_BUILDING);
	for(int i=0; i<1024; ++i)
	{
		Building* b = enemy->myBuildings[i];
		if(b)
		{
			if(b->constructionResultState==Building::NO_CONSTRUCTION)
			{
				unsigned int pos=std::find(ATTACK_PRIORITY, ATTACK_PRIORITY+IntBuildingType::NB_BUILDING-3, b->type->shortTypeNum)-ATTACK_PRIORITY;
				buildings[pos].push_back(b);
			}
		}
	}

	//And now we shuffle then for added randomness
	for(unsigned int i=0; i<buildings.size(); ++i)
	{
		random_shuffle(buildings[i].begin(), buildings[i].end(), syncRandAdapter);
	}

	//Iterate through the buildings, starting attacks as neccecary, and stopping when
	//we run out of available units, or we have reached the maximum number of attacks
	//at once.
	unsigned int attack_count=attacks.size();
	for(vector<vector<Building*> >::iterator i = buildings.begin(); i != buildings.end(); ++i)
	{
		for(vector<Building*>::iterator j = i->begin(); j!=i->end(); ++j)
		{
			Building* b = *j;
			if(attack_count!=MAX_ATTACKS_AT_ONCE && available_units>=ATTACK_WARRIOR_MINIMUM)
			{
				//Make sure where not attacking this building already
				bool found=false;
				for(vector<attackRecord>::iterator i = attacks.begin(); i!=attacks.end(); ++i)
				{
					if(b->gid==i->target)
					{
						found=true;
						break;
					}
				}
				if(found)
					continue;

				//Ok! Launch an attack
				attackRecord ar;
				ar.target=b->gid;
				ar.target_x=b->posX;
				ar.target_y=b->posY;
				ar.flag=NOGBID;
				ar.flagx=b->posX+(b->type->width/2);
				ar.flagy=b->posY+(b->type->height/2);
				ar.zonex=b->posX-ATTACK_ZONE_BUILDING_PADDING;
				ar.zoney=b->posY-ATTACK_ZONE_BUILDING_PADDING;
				ar.width=b->type->width+ATTACK_ZONE_BUILDING_PADDING*2;
				ar.height=b->type->height+ATTACK_ZONE_BUILDING_PADDING*2;
				ar.unitx=b->posX-ATTACK_ZONE_EXAMINATION_PADDING;
				ar.unity=b->posY-ATTACK_ZONE_EXAMINATION_PADDING;
				ar.unit_width=b->type->width+ATTACK_ZONE_EXAMINATION_PADDING*2;
				ar.unit_height=b->type->height+ATTACK_ZONE_EXAMINATION_PADDING*2;
				ar.assigned_units=std::min(std::min(static_cast<unsigned int>(20), available_units), std::max(gps.pollArea(ar.unitx, ar.unity, ar.unit_width, ar.unit_height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_UNITS), ATTACK_WARRIOR_MINIMUM));
				ar.assigned_level=strength_level;
				attacks.push_back(ar);
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: attack: Creating a war flag at "<<ar.flagx<<", "<<ar.flagy<<" and assigning "<<ar.assigned_units<<" units to fight and kill the building at "<<b->posX<<","<<b->posY<<"."<<endl;
				Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("warflag", 0, false);
				ai.orders.push(new OrderCreate(ai.team->teamNumber, ar.flagx, ar.flagy, typeNum));
				++attack_count;
				available_units-=ar.assigned_units;
			}
			else
			{
				return false;
			}
		}
	}
	return false;
}




bool PrioritizedBuildingAttack::updateAttackFlags()
{
	GridPollingSystem gps(ai);

	//Go through all of the buildings, checking each one to see if we have an
	//attack record that is not connected to its flag. If the record is missing
	//its flag, and this building is in the right spot to be the flag that we
	//are missing, then it must be our flag. Merge it into the records, and
	//change it to match up with our records in size, assigned units, and
	//assigned level
	for(int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
		if(b)
		{
			if(b->type->shortTypeNum==IntBuildingType::WAR_FLAG)
			{
				for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end(); j++)
				{
					if(j->flag==NOGBID && b->posX == static_cast<int>(j->flagx) && b->posY == static_cast<int>(j->flagy))
					{
						j->flag=b->gid;
						unsigned int radius=std::max(j->width, j->height)/2;
						if(AINicowar_DEBUG)
							std::cout<<"AINicowar: updateAttackFlags: Found a flag that attack() had created. Giving it a "<<radius<<" radius. Assigning "<<j->assigned_units<<" units to it, and setting it to use level "<<j->assigned_level<<" warriors."<<endl;

						ai.orders.push(new OrderModifyFlag(b->gid, radius));
						ai.orders.push(new OrderModifyBuilding(b->gid, j->assigned_units));
						ai.orders.push(new OrderModifyMinLevelToFlag(b->gid, j->assigned_level));
						ai.getUnitModule()->request("PrioritizedBuildingAttack", WARRIOR, j->assigned_units, ATTACK_STRENGTH, j->assigned_level, UnitModule::medium, j->flag);
						break;
					}
				}
			}
		}
	}

	//Go through the list of records looking for any buildings that where successfully destroyed, erasing the record if neccecary
	for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end();)
	{
		if(j->flag!=NOGBID)
		{
			//We need the additional location checks because sometimes the player where attacking builds a building,
			//which quickly assumes the same gid before we detect and remove the flag here. If we destroyed a building,
			//and the ai quickly remakes the same building in the same spot, we don't stop attacking that spot, there
			//is still a building there.
			Building* b = getBuildingFromGid(ai.game, j->target);
			if((b==NULL || b->posX!=static_cast<int>(j->target_x) || b->posY!=static_cast<int>(j->target_y)))
			{
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: updateAttackFlags: Stopping attack on a building, removing the "<<j->flagx<<","<<j->flagy<<" flag."<<endl;
				ai.orders.push(new OrderDelete(j->flag));
				j=attacks.erase(j);
				continue;
			}
		}
		++j;
	}

	//The following gets the highest barracks level the player has
	unsigned int max_barracks_level=0;
	unsigned int found_barracks=0;
	for(int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
		if(b)
		{
			if(b->type->shortTypeNum==IntBuildingType::ATTACK_BUILDING)
			{
				if(b->constructionResultState==Building::NO_CONSTRUCTION)
				{
					max_barracks_level=std::max(max_barracks_level, static_cast<unsigned int>(b->type->level+1));
					found_barracks++;
				}
			}
		}
	}
	///The average strength level of the teams warriors
	unsigned int average_unit_strength_level=0;
	unsigned int total=0;
	for(unsigned int i=0; static_cast<int>(i)<NB_UNIT_LEVELS; ++i)
	{
		if(i<MINIMUM_BARRACKS_LEVEL+1)
			continue;
		total+=ai.team->stats.getLatestStat()->upgradeState[ATTACK_STRENGTH][i];
		average_unit_strength_level+=ai.team->stats.getLatestStat()->upgradeState[ATTACK_STRENGTH][i]*(i+1);
	}
	if(total>0)
		average_unit_strength_level=static_cast<unsigned int>(std::floor(static_cast<float>(average_unit_strength_level)/static_cast<float>(total)+0.5))-1;
	unsigned int strength_level=USE_MAX_BARRACKS_LEVEL ? max_barracks_level : average_unit_strength_level;

	//Get the number of available units, and go though the record, modifying the number of units assigned to each as
	//neccessary in order to keep up with the defending soldiers
	unsigned int available_units = ai.getUnitModule()->available("PrioritizedBuildingAttack", WARRIOR, ATTACK_STRENGTH, strength_level+1, true, UnitModule::medium);
	for(std::vector<attackRecord>::iterator j = attacks.begin(); j!=attacks.end();)
	{
		if(j->flag != NOGBID)
		{
//			Building* flag=getBuildingFromGid(ai.game, j->flag);
			//Add the number of units that are assigned to this flag to the total number of units available
			available_units+=j->assigned_units;

			unsigned int score = gps.pollArea(j->unitx, j->unity, j->unit_width, j->unit_height, GridPollingSystem::MAXIMUM,
				GridPollingSystem::ENEMY_UNITS);
			unsigned int new_assigned=std::min(std::min(static_cast<unsigned int>(20), available_units),
				std::max(score, ATTACK_WARRIOR_MINIMUM));

			//If we don't have enough free units to continue the fight, remove the flag
			if(new_assigned<ATTACK_WARRIOR_MINIMUM)
			{
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: updateAttackFlags: Stopping attack, not enough free warriors, removing the "<<j->flagx<<","<<j->flagy<<" flag."<<endl;
				ai.orders.push(new OrderDelete(j->flag));
				j=attacks.erase(j);
				continue;
			}

			//If the number of units that should be attacking the building has changed,
			//update the flag.
			if(new_assigned != j->assigned_units)
			{
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: updateAttackFlags: Changing the "<<j->flagx<<","<<j->flagy<<" flag from "<<j->assigned_units<<" to "<<new_assigned<<"."<<endl;
				j->assigned_units=new_assigned;
				ai.orders.push(new OrderModifyBuilding(j->flag, new_assigned));
				available_units-=new_assigned;
				ai.getUnitModule()->request("PrioritizedBuildingAttack", WARRIOR, new_assigned, ATTACK_STRENGTH, strength_level, UnitModule::medium, j->flag);
			}

			//If the maximum barracks level has changed, then update the flag
			if(strength_level != j->assigned_level)
			{
				j->assigned_level=strength_level;
				ai.orders.push(new OrderModifyMinLevelToFlag(j->flag, strength_level));
			}
		}
		++j;
	}

	return false;
}




DistributedNewConstructionManager::DistributedNewConstructionManager(AINicowar& ai) : ai(ai)
{
	ai.setNewConstructionModule(this);
}




bool DistributedNewConstructionManager::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return constructBuildings();
		case 1:
			return updateBuildings();
		case 2:
			return calculateBuildings();
	}
	return false;
}




string DistributedNewConstructionManager::getName() const
{
	return "DistributedNewConstructionManager";
}




bool DistributedNewConstructionManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("DistributedNewConstructionManager");
	stream->readEnterSection("new_buildings");
	Uint32 newConstructionRecordSize = stream->readUint32("newConstructionRecordSize");
	for (Uint32 newConstructionRecordIndex = 0; newConstructionRecordIndex < newConstructionRecordSize; newConstructionRecordIndex++)
	{
		stream->readEnterSection(newConstructionRecordIndex);
		newConstructionRecord ncr;
		ncr.building = stream->readUint32("building");
		ncr.x = stream->readUint32("x");
		ncr.y = stream->readUint32("y");
		ncr.assigned = stream->readUint32("assigned");
		ncr.building_type = stream->readUint32("building_type");
		new_buildings.push_back(ncr);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("num_buildings_wanted");
	Uint32 numBuildingWantedSize = stream->readUint32("size");
	for (Uint32 numBuildingWantedIndex = 0; numBuildingWantedIndex < numBuildingWantedSize; numBuildingWantedIndex++)
	{
		stream->readEnterSection(numBuildingWantedIndex);
		num_buildings_wanted[stream->readUint32("first")] = stream->readUint32("second");
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}

	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void DistributedNewConstructionManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("DistributedNewConstructionManager");
	stream->writeEnterSection("new_buildings");
	stream->writeUint32(new_buildings.size(), "newConstructionRecordSize");
	for (Uint32 newConstructionRecordIndex = 0; newConstructionRecordIndex < new_buildings.size(); newConstructionRecordIndex++)
	{
		stream->writeEnterSection(newConstructionRecordIndex);
		std::vector<newConstructionRecord>::const_iterator i = new_buildings.begin() + newConstructionRecordIndex;
		stream->writeUint32(i->building, "building");
		stream->writeUint32(i->x, "x");
		stream->writeUint32(i->y, "y");
		stream->writeUint32(i->assigned, "assigned");
		stream->writeUint32(i->building_type, "building_type");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("num_buildings_wanted");
	stream->writeUint32(num_buildings_wanted.size(), "size");
	Uint32 numBuildingWantedIndex = 0;
	for (std::map<unsigned int, unsigned int>::const_iterator i = num_buildings_wanted.begin(); i != num_buildings_wanted.begin(); ++i)
	{
		stream->writeEnterSection(numBuildingWantedIndex++);
		stream->writeUint32(i->first, "first");
		stream->writeUint32(i->second, "second");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




DistributedNewConstructionManager::upgradeData DistributedNewConstructionManager::findMaxSize(unsigned int building_type, unsigned int cur_level)
{
	string type = IntBuildingType::reverseConversionMap[building_type];
	BuildingType* new_type = globalContainer->buildingsTypes.getByType(type, 2, false);
	BuildingType* cur_type = globalContainer->buildingsTypes.getByType(type, cur_level, false);
	upgradeData ud;
	if(new_type)
	{
		ud.width=new_type->width;
		ud.height=new_type->height;
		ud.horizontal_offset = (new_type->width - cur_type->width)/2;
		ud.vertical_offset = (new_type->height - cur_type->height)/2;
	}
	else
	{
		ud.width=cur_type->width;
		ud.height=cur_type->height;
		ud.horizontal_offset=0;
		ud.vertical_offset=0;
	}
	return ud;
}




DistributedNewConstructionManager::point DistributedNewConstructionManager::findBestPlace(unsigned int building_type)
{
	GridPollingSystem gps(ai);
	//Get the best zone based on a number of factors
	GridPollingSystem::poll zp;
	zp.mod_1=static_cast<GridPollingSystem::pollModifier>(CONSTRUCTION_FACTORS[building_type][0][0]);
	zp.type_1=static_cast<GridPollingSystem::pollType>(CONSTRUCTION_FACTORS[building_type][0][1]);
	zp.mod_2=static_cast<GridPollingSystem::pollModifier>(CONSTRUCTION_FACTORS[building_type][1][0]);
	zp.type_2=static_cast<GridPollingSystem::pollType>(CONSTRUCTION_FACTORS[building_type][1][1]);
	zp.mod_3=static_cast<GridPollingSystem::pollModifier>(CONSTRUCTION_FACTORS[building_type][2][0]);
	zp.type_3=static_cast<GridPollingSystem::pollType>(CONSTRUCTION_FACTORS[building_type][2][1]);
	zp.is_strict_minimum=STRICT_NEARBY_BUILDING_MINIMUM;
	zp.mod_minimum=GridPollingSystem::MAXIMUM;
	zp.minimum_type=GridPollingSystem::FRIENDLY_BUILDINGS;
	zp.minimum_score=MINIMUM_NEARBY_BUILDINGS_TO_CONSTRUCT;

	GridPollingSystem::getBestZonesSplit* split = gps.getBestZones(zp, BUILD_AREA_WIDTH, BUILD_AREA_HEIGHT, BUILD_AREA_HORIZONTAL_OVERLAP,
		BUILD_AREA_VERTICAL_OVERLAP, BUILD_AREA_EXTENTION_WIDTH, BUILD_AREA_EXTENTION_HEIGHT);

	vector<GridPollingSystem::zone> zones = gps.getBestZones(split);

	upgradeData size=findMaxSize(building_type, 0);

	//Iterate through the zones
	for(std::vector<GridPollingSystem::zone>::iterator i = zones.begin(); i!=zones.end(); ++i)
	{
		//Check if this zone is in the cache already, and if it is, and the building where finding is larger than the one recorded
		//in the cache, skip this zone.
		if(no_build_cache.find(*i)!=no_build_cache.end())
		{
			noBuildRecord nbr=no_build_cache[*i];
			if(size.width>=nbr.min_width && size.height>=nbr.min_height)
			{
				continue;
			}
		}

		//Pregenerate some common numbers
		const unsigned int full_width=BUILD_AREA_WIDTH+2*BUILD_AREA_EXTENTION_WIDTH;
		const unsigned int full_height=BUILD_AREA_HEIGHT+2*BUILD_AREA_EXTENTION_HEIGHT;
		const unsigned int zone_start_column=BUILD_AREA_EXTENTION_WIDTH;
		const unsigned int zone_start_row=BUILD_AREA_EXTENTION_HEIGHT;
		const unsigned int zone_end_column=BUILD_AREA_EXTENTION_WIDTH+BUILD_AREA_WIDTH;
		const unsigned int zone_end_row=BUILD_AREA_EXTENTION_HEIGHT+BUILD_AREA_HEIGHT;
		unsigned short int* imap = new unsigned short int[full_width*full_height];

		//Prepare the imap
		for(unsigned int x=0; x<full_width; ++x)
		{
			for(unsigned int y=0; y<full_height; ++y)
			{
				imap[x*full_width+y]=0;
			}
		}

		//Go through each square checking off squares that are used
		for(unsigned int x=0; x<full_width; ++x)
		{
			unsigned int fx=i->x + x - BUILD_AREA_EXTENTION_WIDTH;
			for(unsigned int y=0; y<full_height; ++y)
			{
				unsigned int fy=i->y + y - BUILD_AREA_EXTENTION_HEIGHT;
				//Check off hidden or occupied squares
				if(!ai.map->isHardSpaceForBuilding(fx, fy) || !ai.map->isMapDiscovered(fx, fy, ai.team->me))
				{
					imap[x*full_width+y]=1;
				}

				//If we find a building here (or a building that is in the list of buildings to be constructed)
				bool found_building=false;
				upgradeData bsize;
				if(ai.map->getBuilding(fx, fy)!=NOGBID)
				{
					Building* b=getBuildingFromGid(ai.game, ai.map->getBuilding(fx, fy));
					bsize=findMaxSize(b->type->shortTypeNum, b->type->level);
					found_building=true;
				}

				for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end(); i++)
				{
					if(i->x == fx && i->y == fy)
					{
						found_building=true;
						bsize=findMaxSize(i->building_type, 0);
						break;
					}
				}
				//Then mark all the squares this building occupies in its largest upgrade with an extra padding
				if(found_building)
				{

					unsigned int startx=x-bsize.horizontal_offset-BUILDING_PADDING;
					if(startx<0)
						startx=0;
					unsigned int starty=y-bsize.vertical_offset-BUILDING_PADDING;
					if(starty<0)
						starty=0;
					unsigned int endx=startx+bsize.width+BUILDING_PADDING*2;
					if(endx>=full_width)
						endx=full_width;
					unsigned int endy=starty+bsize.height+BUILDING_PADDING*2;
					if(endy>=full_height)
						endy=full_height;
					for(unsigned int x2=startx; x2<endx; ++x2)
					{
						for(unsigned int y2=starty; y2<endy; ++y2)
						{
							imap[x2*full_width+y2]=1;
						}
					}
				}
			}
		}

		//Change this to output the int maps for debugging, carefull, they are large

		const bool output_zones=false;
		if(output_zones)
		{
			std::cout<<"Zone "<<i->x<<","<<i->y<<endl;
			for(unsigned int y=0; y<full_height; ++y)
			{

				if(y==zone_start_row || y==zone_end_row)
				{
					for(unsigned int x=0; x<full_width+2; ++x)
					{
						if (x>=zone_start_column+1 && x<zone_end_column+1)
							std::cout<<"--";
						else
							std::cout<<"  ";
					}
					std::cout<<endl;
				}

				for(unsigned int x=0; x<full_width; ++x)
				{
					if (x==zone_start_column || x==zone_end_column)
					{
						if(y >= zone_start_row && y<zone_end_row)
							std::cout<<"| ";
						else
							std::cout<<"  ";
					}
					std::cout<<imap[x*full_width+y]<<" ";
				}
				std::cout<<endl;
			}
		}

		//Now go through the zones int map searching for a good, possible place to put the building.
		unsigned int max=0;
		point max_point;
		max_point.x=NOPOS;
		max_point.y=NOPOS;
		for(unsigned int x=zone_start_column; x<zone_end_column; ++x)
		{
			for(unsigned int y=zone_start_row; y<zone_end_row; ++y)
			{
				if(imap[x*full_width+y]==0)
				{
					bool all_empty=true;
					for(unsigned int x2=x; x2<(x+size.width) && all_empty; ++x2)
					{
						for(unsigned int y2=y; y2<(y+size.height) && all_empty; ++y2)
						{
							if(imap[x2*full_width+y2]==1)
							{
								all_empty=false;
							}
						}
					}
					if(all_empty)
					{
						point p;
						p.x=i->x + x - BUILD_AREA_EXTENTION_WIDTH;
						p.y=i->y + y - BUILD_AREA_EXTENTION_HEIGHT;
						if(!CRAMP_BUILDINGS)
							return p;

						unsigned int border_count=0;
						for(unsigned int x2=x-1; x2<(x+size.width+2); ++x2)
						{
							for(unsigned int y2=y-1; y2<(y+size.height+2); ++y2)
							{
								if(imap[x2*full_width+y2]==1)
									border_count++;
							}
						}
						if(border_count>max || max==0)
						{
							max_point=p;
							max=border_count;
						}
					}
				}
			}
		}

		if(max_point.x!=NOPOS && CRAMP_BUILDINGS)
			return max_point;

		///Add this zone to our cache if we can not build here
		if(max_point.x==NOPOS || max_point.y==NOPOS)
		{
			noBuildRecord nbr;
			if(no_build_cache.find(*i)!=no_build_cache.end())
			{
				nbr=no_build_cache[*i];
				if(size.width<=nbr.min_width && size.height<=nbr.min_height)
				{
					nbr.min_width=std::min(nbr.min_width, size.width);
					nbr.min_height=std::min(nbr.min_height, size.height);
				}
			}
			else
			{
				nbr.min_width=size.width;
				nbr.min_height=size.height;
				nbr.turns=0;
			}
			no_build_cache[*i]=nbr;
		}

	}
	point p;
	p.x=NOPOS;
	p.y=NOPOS;
	return p;
}




bool DistributedNewConstructionManager::constructBuildings()
{
	updateNoBuildCache();

	ai.getUnitModule()->changeUnits("DistributedNewConstructionManager", WORKER, MAXIMUM_TO_CONSTRUCT_NEW*MAX_NEW_CONSTRUCTION_AT_ONCE, BUILD, 1, UnitModule::medium);

	//Enabling this will turn on verbose debugging mode for this function,
	//it will tell you various things like why its not cosntructing buildings
	//for various reasons.
	const bool local_debug=false;

	//Get the total number of free workers, since a worker of any level can construct a new building
	unsigned total_free_workers=ai.getUnitModule()->available("DistributedNewConstructionManager", WORKER, BUILD, 1, true, UnitModule::medium);

	//Counts out the number of buildings allready existing, including ones under construction
	unsigned int counts[IntBuildingType::NB_BUILDING];

	//The totals of the number of builinds under construction only
	unsigned int under_construction_counts[IntBuildingType::NB_BUILDING];

	//The total amount of construction
	unsigned int total_construction=new_buildings.size();

	//Holds the order in which it should decide to build buildings.
	std::vector<typePercent> construction_priorities;

	if(total_construction>=MAX_NEW_CONSTRUCTION_AT_ONCE)
	{
		if(local_debug)
			std::cout<<"Fail 1, too much construction."<<endl;
		return false;
	}

	//Count up the numbers of buildings not under construction on a per-building type basis
	for(unsigned i = 0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		counts[i]=ai.team->stats.getLatestStat()->numberBuildingPerType[i];
		under_construction_counts[i]=0;
	}

	//Now count up the number of buildings under construction, as well adding to the number of buildings
	//not under construction, and taking away any units that are needed for other construction sites
	//from the total number of free units. Ignore buildings
	for(std::vector<newConstructionRecord>::iterator i=new_buildings.begin(); i!=new_buildings.end(); ++i)
	{
		counts[i->building_type]++;
		under_construction_counts[i->building_type]++;
		if(total_free_workers>i->assigned)
			total_free_workers-=i->assigned;
		else
			total_free_workers=0;

		if(i->building!=NOGBID && getBuildingFromGid(ai.game, i->building)!=NULL)
			total_free_workers+=getBuildingFromGid(ai.game, i->building)->unitsWorking.size();
	}

	//Develop a list of percentages used for sorting what buildings this ai should develop first, and sort this list
	for (unsigned int i = 0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		typePercent tp;
		tp.building_type=i;
		if(num_buildings_wanted[i]==0)
			tp.percent=100;
		else
			tp.percent=static_cast<unsigned int>(static_cast<float>(counts[i])/static_cast<float>(num_buildings_wanted[i])*100);
		construction_priorities.push_back(tp);
	}
	std::sort(construction_priorities.begin(), construction_priorities.end());

	//These numbers are big because I don't believe a building size will ever exceed them.
	unsigned int min_failed_width=512;
	unsigned int min_failed_height=512;
	for(std::vector<typePercent>::iterator i = construction_priorities.begin(); i!=construction_priorities.end(); ++i)
	{
		//Keep constructing buildings of this type untill one of the failure conditions have been reached
		while(true)
		{
			if(total_construction>=MAX_NEW_CONSTRUCTION_AT_ONCE)
			{
				if(local_debug)
					std::cout<<"Fail 1, too much construction, for "<<IntBuildingType::reverseConversionMap[i->building_type]<<"."<<endl;
				return false;
			}
			if(total_free_workers<MINIMUM_TO_CONSTRUCT_NEW && !CHEAT_INSTANT_BUILDING)
			{
				if(local_debug)
					std::cout<<"Fail 2, too few units, for "<<IntBuildingType::reverseConversionMap[i->building_type]<<"."<<endl;
				return false;
			}
			if(under_construction_counts[i->building_type]>=MAX_NEW_CONSTRUCTION_PER_BUILDING[i->building_type])
			{
				if(local_debug)
					std::cout<<"Fail 3, too many buildings of this type under constructon, for "<<IntBuildingType::reverseConversionMap[i->building_type]<<"."<<endl;
				break;
			}
			if(counts[i->building_type]>=num_buildings_wanted[i->building_type])
			{
				if(local_debug)
					std::cout<<"Fail 4, building cap reached, for "<<IntBuildingType::reverseConversionMap[i->building_type]<<"."<<endl;
				break;
			}

			//Find the largest size that this building can have
			point p = findBestPlace(i->building_type);
			upgradeData size=findMaxSize(i->building_type, 0);
			//If there wasn't a place for a smaller building than this one, than there certainly won't be a place for this one
			if(size.width>=min_failed_width && size.height>=min_failed_height)
				break;
			if(p.x == NOPOS || p.y==NOPOS)
			{
				min_failed_width=size.width;
				min_failed_height=size.height;

				if(local_debug)
					std::cout<<"Fail 5, no suitable positon found, for "<<IntBuildingType::reverseConversionMap[i->building_type]<<"."<<endl;
				break;
			}

			//We have the ok on everything, start constructing a building
			newConstructionRecord ncr;
			ncr.building=NOGBID;
			ncr.x=p.x;
			ncr.y=p.y;

			//Don't assign more units than the total amount of resources needed to construct the building.
			unsigned int needed_resource_total=0;
			BuildingType* t=globalContainer->buildingsTypes.getByType(IntBuildingType::reverseConversionMap[i->building_type], 0, true);
			for(unsigned int n=0; n<MAX_NB_RESSOURCES; ++n)
			{
				needed_resource_total+=t->maxRessource[n];
			}
			ncr.assigned=std::min(total_free_workers, std::min(MAXIMUM_TO_CONSTRUCT_NEW, needed_resource_total));

			ncr.building_type=i->building_type;
			new_buildings.push_back(ncr);

			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: constructBuildings: Starting construction on a "<<IntBuildingType::reverseConversionMap[i->building_type]<<", at position "<<p.x<<","<<p.y<<"."<<endl;
			Sint32 type=globalContainer->buildingsTypes.getTypeNum(IntBuildingType::reverseConversionMap[i->building_type], 0, !CHEAT_INSTANT_BUILDING);
			ai.orders.push(new OrderCreate(ai.team->teamNumber, p.x, p.y, type));
			total_construction+=1;
			total_free_workers-=ncr.assigned;
			under_construction_counts[i->building_type]++;
			counts[i->building_type]++;
		}
	}
	return false;
}




bool DistributedNewConstructionManager::updateBuildings()
{
	//Remove records of buildings that are no longer under construction, or ones for buildings that where destroyed be the enemy
	for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end();)
	{
		if(i->building!=NOGBID)
		{
			Building* b = getBuildingFromGid(ai.game, i->building);
			if(b==NULL || b->constructionResultState == Building::NO_CONSTRUCTION)
			{
				///If its been changed since it was finished (perhaps by another module),
				///don't undo the changes
				if(b!=NULL && b->maxUnitWorking==static_cast<int>(i->assigned))
					ai.orders.push(new OrderModifyBuilding(i->building, 1));
				ai.getUnitModule()->request("DistributedNewConstructionManager", WORKER, BUILD, 1, 0, UnitModule::medium, i->building);
				i = new_buildings.erase(i);
				continue;
			}
		}
		++i;
	}

	//Update buildings that have just been created.
	//There is one problem, if a building is destroyed by enemy troops before this gets executed, the new record will hang and never be removed, causing
	//disruptions throughout the code.
	for(int i=0; i<1024; ++i)
	{
		Building *b = ai.team->myBuildings[i];
		if(b)
		{
			for(std::vector<newConstructionRecord>::iterator i = new_buildings.begin(); i != new_buildings.end(); ++i)
			{
				if(i->building==NOGBID && b->type->shortTypeNum == static_cast<int>(i->building_type) && b->posX == static_cast<int>(i->x) && b->posY == static_cast<int>(i->y))
				{
					i->building=b->gid;
					ai.orders.push(new OrderModifyBuilding(i->building, i->assigned));
					ai.getUnitModule()->request("DistributedNewConstructionManager", WORKER, BUILD, 1, i->assigned, UnitModule::medium, i->building);
				}
			}
		}
	}
	return false;
}




bool DistributedNewConstructionManager::calculateBuildings()
{
	unsigned int total_units=ai.team->stats.getLatestStat()->totalUnit;
	for (int i=0; i<IntBuildingType::NB_BUILDING; ++i)
	{
		if(UNITS_FOR_BUILDING[i]!=0)
			num_buildings_wanted[i]=total_units/UNITS_FOR_BUILDING[i]+1;
		else
			num_buildings_wanted[i]=0;
	}
	return false;
}




void DistributedNewConstructionManager::updateNoBuildCache()
{
	for(std::map<GridPollingSystem::zone, noBuildRecord>::iterator i=no_build_cache.begin(); i!=no_build_cache.end(); ++i)
	{
		i->second.turns++;
		if(i->second.turns > NO_BUILD_CACHE_TIMEOUT)
			no_build_cache.erase(i);
	}
}




bool DistributedNewConstructionManager::typePercent::operator<(const typePercent& tp) const
{
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]>STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return true;
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]<STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return false;

	return percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[building_type]<tp.percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[tp.building_type];
}




bool DistributedNewConstructionManager::typePercent::operator>(const typePercent& tp) const
{
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]<STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return true;
	if(STRICT_NEW_CONSTRUCTION_PRIORITIES[building_type]>STRICT_NEW_CONSTRUCTION_PRIORITIES[tp.building_type])
		return false;
	return percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[building_type]>tp.percent*WEAK_NEW_CONSTRUCTION_PRIORITIES[tp.building_type];
}




RandomUpgradeRepairModule::RandomUpgradeRepairModule(AINicowar& ai) : ai(ai)
{
	ai.setUpgradeRepairModule(this);
}




bool RandomUpgradeRepairModule::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return removeOldConstruction();
		case 1:
			return updatePendingConstruction();
		case 2:
			return startNewConstruction();
		case 3:
			return reassignConstruction();
	}
	return false;
}




string RandomUpgradeRepairModule::getName() const
{
	return "RandomUpgradeRepairModule";
}




bool RandomUpgradeRepairModule::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("RandomUpgradeRepairModule");
	stream->readEnterSection("active_construction");
	Uint32 constructionRecordSize = stream->readUint32("size");
	for (Uint32 constructionRecordIndex = 0; constructionRecordIndex < constructionRecordSize; constructionRecordIndex++)
	{
		stream->readEnterSection(constructionRecordIndex);
		constructionRecord cr;
		cr.building=getBuildingFromGid(ai.game, stream->readUint32("gid"));
		cr.assigned=stream->readUint32("assigned");
		cr.original=stream->readUint32("original");
		cr.is_repair=stream->readUint8("is_repair");
		active_construction.push_back(cr);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("pending_construction");
	constructionRecordSize = stream->readUint32("size");
	for (Uint32 constructionRecordIndex = 0; constructionRecordIndex < constructionRecordSize; constructionRecordIndex++)
	{
		stream->readEnterSection(constructionRecordIndex);
		constructionRecord cr;
		cr.building=getBuildingFromGid(ai.game, stream->readUint32("gid"));
		cr.assigned=stream->readUint32("assigned");
		cr.original=stream->readUint32("original");
		cr.is_repair=stream->readUint8("is_repair");
		pending_construction.push_back(cr);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void RandomUpgradeRepairModule::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("RandomUpgradeRepairModule");
	stream->writeEnterSection("active_construction");
	stream->writeUint32(active_construction.size(), "size");
	Uint32 constructionRecordIndex = 0;
	for(std::list<constructionRecord>::const_iterator i = active_construction.begin(); i!=active_construction.end(); ++i)
	{
		stream->writeEnterSection(constructionRecordIndex++);
		stream->writeUint32(i->building->gid, "gid");
		stream->writeUint32(i->assigned, "assigned");
		stream->writeUint32(i->original, "original");
		stream->writeUint8(i->is_repair, "is_repair");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("pending_construction");
	stream->writeUint32(pending_construction.size(), "size");
	constructionRecordIndex = 0;
	for(std::list<constructionRecord>::const_iterator i = pending_construction.begin(); i!=pending_construction.end(); ++i)
	{
		stream->writeEnterSection(constructionRecordIndex++);
		stream->writeUint32(i->building->gid, "gid");
		stream->writeUint32(i->assigned, "assigned");
		stream->writeUint32(i->original, "original");
		stream->writeUint8(i->is_repair, "is_repair");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




bool RandomUpgradeRepairModule::removeOldConstruction(void)
{
	for (std::list<constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end();)
	{
		Building *b=i->building;
		if(!buildingStillExists(ai.game, b))
		{
			i=active_construction.erase(i);
			continue;
		}

		unsigned int original = i->original;
		if (b->constructionResultState!=Building::UPGRADE && b->constructionResultState!=Building::REPAIR )
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: removeOldConstruction: Removing an old "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" from the active construction list, changing assigned number of units back to "<<original<<" from "<<i->assigned<<"."<<endl;
			ai.getUnitModule()->request("RandomUpgradeRepairModule", WORKER, BUILD, b->type->level+1, 0, UnitModule::medium, b->gid);
			i=active_construction.erase(i);
			ai.orders.push(new OrderModifyBuilding(b->gid, original));
			continue;
		}
		i++;
	}
	return false;
}




bool RandomUpgradeRepairModule::updatePendingConstruction(void)
{
	for (std::list<constructionRecord>::iterator i = pending_construction.begin(); i!=pending_construction.end();)
	{
		Building *b=i->building;
		if(!buildingStillExists(ai.game, b))
		{
			i=active_construction.erase(i);
			continue;
		}
		unsigned int assigned = i->assigned;
		if (b->buildingState != Building::WAITING_FOR_CONSTRUCTION && b->buildingState != Building::WAITING_FOR_CONSTRUCTION_ROOM)
		{
			constructionRecord u=*i;
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: updatePendingConstruction: The "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" was found that it is no longer pending construction, I am assigning number of requested units, "<<assigned<<", to it."<<endl;
			active_construction.push_back(u);
			i=pending_construction.erase(i);
			ai.orders.push(new OrderModifyBuilding(b->gid, assigned));
			continue;
		}
		i++;
	}
	return false;
}




bool RandomUpgradeRepairModule::reassignConstruction(void)
{
	//Get the numbers of free units
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=ai.getUnitModule()->available("RandomUpgradeRepairModule", WORKER, BUILD, j+1, false, UnitModule::medium);
	}

	//Finally, iterate through the shuffled list of records changing the number of units allocated to upgrade the buildings.
	for (list<constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		Building *b=i->building;
		if(!buildingStillExists(ai.game, b))
			continue;
		if(b->constructionResultState!=Building::UPGRADE && b->constructionResultState!=Building::REPAIR)
			continue;

		free_workers[b->type->level]+=b->maxUnitWorking;

		unsigned int assigned=i->assigned;
		bool is_repair=i->is_repair;

		//Find the number of workers with enough of a level to upgrade this building
		int available_upgrade = 0;
		int available_repair  = 0;
		for (int j = 0; j<NB_UNIT_LEVELS; j++)
		{
			//Important, b->type->level takes on the level number of the building its being upgraded to.
			//So in this function, b->type->level will be different then in startNewConstruction for
			//buildings being upgraded. I have copy and pasted this section of code a thousand times
			//between the two when I change it and each time it causes me trouble.
			if (j > (b->type->level)-1 )
			{
				available_upgrade+=free_workers[j];
			}
			if (j >= (b->type->level) )
			{
				available_repair+=free_workers[j];
			}
		}

		if (!is_repair && available_upgrade==0)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: reassignConstruction: There are not enough available units. Canceling upgrade on the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<"."<<endl;
			ai.getUnitModule()->request("RandomUpgradeRepairModule", WORKER, BUILD, b->type->level+1, 0, UnitModule::medium, b->gid);
			ai.orders.push(new OrderCancelConstruction(b->gid));
			continue;
		}

		else if (is_repair && available_repair==0)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: reassignConstruction: There are not enough available units. Canceling repair on the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<"."<<endl;
			ai.getUnitModule()->request("RandomUpgradeRepairModule", WORKER, BUILD, b->type->level+1, 0, UnitModule::medium, b->gid);
			ai.orders.push(new OrderCancelConstruction(b->gid));
			continue;
		}

		//Issue the command to change the number of units working on the building to the new amount
		unsigned int num_to_assign=0;
		unsigned int generic_available=0;
		if (!is_repair)
		{
			num_to_assign=available_upgrade;
			generic_available=available_upgrade;
			if (num_to_assign>MAXIMUM_TO_UPGRADE)
				num_to_assign=MAXIMUM_TO_UPGRADE;
		}
		else if (is_repair)
		{
			num_to_assign=available_repair;
			generic_available=available_repair;
			if (num_to_assign>MAXIMUM_TO_REPAIR)
				num_to_assign=MAXIMUM_TO_REPAIR;
		}

		if (num_to_assign != assigned)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: reassignConstruction: Retasking "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" that is under construction. Number of units available: "<<generic_available<< ". Number of units originally assigned: "<<assigned<<". Number of units assigning: "<<num_to_assign<<"."<<endl;
			ai.getUnitModule()->request("RandomUpgradeRepairModule", WORKER, BUILD, b->type->level+1, num_to_assign, UnitModule::medium, b->gid);
			ai.orders.push(new OrderModifyBuilding(b->gid, num_to_assign));
			i->assigned=num_to_assign;
		}
		reduce(free_workers, b->type->level, num_to_assign);
	}
	return false;
}




bool RandomUpgradeRepairModule::startNewConstruction(void)
{
	ai.getUnitModule()->changeUnits("RandomUpgradeRepairModule", WORKER, MAXIMUM_TO_UPGRADE*MAX_CONSTRUCTION_AT_ONCE, BUILD, 2, UnitModule::medium);

	//If we already have more than our max, don't do any more
	if (active_construction.size()+pending_construction.size()>=MAX_CONSTRUCTION_AT_ONCE)
		return false;

	//This variable indicates the ratios of the demands for various types of buildings to be upgraded,
	//it will be used to calculate parameters for UnitModule::changeUnits, and in turn will distribute
	//the construction of various buildings.
	unsigned int ratios[3];
	for(int i=0; i<3; ++i)
		ratios[i]=0;
	//Look through each building we previoussly assigned to be upgraded, and take note of the type of building, counting up the numbers for each type.
	int construction_counts[IntBuildingType::NB_BUILDING];
	for (int j=0; j!=IntBuildingType::NB_BUILDING; j++)
		construction_counts[j]=0;

	for (list<constructionRecord>::iterator i = active_construction.begin(); i!=active_construction.end(); i++)
	{
		Building *b=i->building;
		if(buildingStillExists(ai.game, b))
		{
			ratios[b->type->level]+=1;
			construction_counts[b->type->shortTypeNum]+=1;
		}
	}

	for (list<constructionRecord>::iterator i = pending_construction.begin(); i!=pending_construction.end(); i++)
	{
		Building *b=i->building;
		if(buildingStillExists(ai.game, b))
		{
			ratios[b->type->level]+=1;
			construction_counts[b->type->shortTypeNum]+=1;
		}
	}

	//Make a copy of the array of buildings, then shuffle it so that every building has a random chance of being upgraded with the spare units.
	///Also count up the numbers of buildings for each level to allow for ratio'ing and divy up the construction limits between various levels
	vector<Building*> buildings;
	buildings.reserve(1024);
	for (int i=0; i<1024; i+=1)
	{
		Building *b=ai.team->myBuildings[i];
		if (b)
		{
			if(b->constructionResultState==Building::NO_CONSTRUCTION           &&
				b->buildingState==Building::ALIVE                          &&
				construction_counts[b->type->shortTypeNum] != MAX_BUILDING_SPECIFIC_CONSTRUCTION_LIMITS[b->type->shortTypeNum])
			{
				if (b->type->shortTypeNum!=IntBuildingType::SWARM_BUILDING             &&
					b->type->shortTypeNum!=IntBuildingType::EXPLORATION_FLAG       &&
					b->type->shortTypeNum!=IntBuildingType::WAR_FLAG               &&
					b->type->shortTypeNum!=IntBuildingType::CLEARING_FLAG          &&
					b->hp  <  b->type->hpMax)
				{
					buildings.push_back(b);
					ratios[b->type->level]+=1;
				}

				else if (b->type->level!=2                                     &&
					(b->type->shortTypeNum==IntBuildingType::FOOD_BUILDING     ||
					b->type->shortTypeNum==IntBuildingType::HEAL_BUILDING      ||
					b->type->shortTypeNum==IntBuildingType::WALKSPEED_BUILDING ||
					b->type->shortTypeNum==IntBuildingType::SWIMSPEED_BUILDING ||
					b->type->shortTypeNum==IntBuildingType::ATTACK_BUILDING    ||
					b->type->shortTypeNum==IntBuildingType::SCIENCE_BUILDING   ||
					b->type->shortTypeNum==IntBuildingType::DEFENSE_BUILDING  ))
				{
					buildings.push_back(b);
					ratios[b->type->level+1]+=1;
				}
			}
		}
	}
	selection_sort(buildings.begin(), buildings.end(), weighted_random_upgrade_comparison);

	unsigned int ratio_total=std::accumulate(ratios, ratios+3, 0);
	float devisor=static_cast<float>(ratio_total)/MAX_CONSTRUCTION_AT_ONCE;
	for(int i=0; i<3; ++i)
	{
		ratios[i]=static_cast<unsigned int>(static_cast<float>(ratios[i])/devisor);
		ai.getUnitModule()->changeUnits("RandomUpgradeRepairModule", WORKER, ratios[i]*MAX_CONSTRUCTION_AT_ONCE, BUILD, i+1, UnitModule::medium);
	}

	//Get the numbers of free units
	int free_workers[NB_UNIT_LEVELS];
	for (int j=0; j<NB_UNIT_LEVELS; j++)
	{
		free_workers[j]=ai.getUnitModule()->available("RandomUpgradeRepairModule", WORKER, BUILD, j+1, false, UnitModule::medium);
	}

	//Look through the buildings, and if their are atleast 4 of the correct unit type available to upgrade/repair it, then do it.
	for (vector<Building*>::iterator i = buildings.begin(); i!=buildings.end(); i++)
	{
		Building *b=*i;
			//Check if its worthy of having anything done to it.
			{
				//Find the number of workers with enough of a level to upgrade and repair this building (if it will need it).
				unsigned int available_upgrade = 0;
				unsigned int available_repair  = 0;
				for (int j = 0; j<NB_UNIT_LEVELS; j++)
				{
					if (j > (b->type->level) )
					{
						available_upgrade+=free_workers[j];
					}
					if (j >= (b->type->level) )
					{
						available_repair+=free_workers[j];
					}
				}

				//See if the building needs repair. Note the code for initializing a repair and the code for initializing an upgrade are very similiar.
				if (b->type->shortTypeNum!=IntBuildingType::CLEARING_FLAG          &&
					available_repair>=MINIMUM_TO_REPAIR                            &&
					b->hp  <  b->type->hpMax)
				{
					unsigned int num_to_assign=available_repair;
					if( num_to_assign > MAXIMUM_TO_REPAIR)
						num_to_assign=MAXIMUM_TO_REPAIR;
					if(AINicowar_DEBUG)
						cout<<"AINicowar: startNewConstruction: Found "<<available_repair<<" available workers, assigning "<<num_to_assign<<" workers to repair the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" with "<<b->maxUnitWorking<<" units already working on it."<<endl;
					constructionRecord u;
					u.building=b;
					u.assigned=num_to_assign;
					u.original=b->maxUnitWorking;
					u.is_repair=true;
					pending_construction.push_back(u);
					ai.orders.push(new OrderConstruction(b->gid));
					ai.getUnitModule()->request("RandomUpgradeRepairModule", WORKER, BUILD, b->type->level+1, u.assigned, UnitModule::medium, b->gid);
					reduce(free_workers, b->type->level, num_to_assign);
					construction_counts[b->type->shortTypeNum]+=1;
				}

				//If we don't need to repair, see if we can upgrade
				else if (available_upgrade>=MINIMUM_TO_UPGRADE)
				{

					//If we have a minimum of 4 available workers, upgrade the building.
					unsigned int num_to_assign=available_upgrade;
					if( num_to_assign > MAXIMUM_TO_UPGRADE)
						num_to_assign=MAXIMUM_TO_UPGRADE;
					if(AINicowar_DEBUG)
						cout<<"AINicowar: startNewConstruction: Found "<<available_upgrade<<" available workers, assigning "<<num_to_assign<<" workers to upgrade the "<<IntBuildingType::typeFromShortNumber(b->type->shortTypeNum)<<" with "<<b->maxUnitWorking<<" units already working on it."<<endl;
					constructionRecord u;
					u.building=b;
					u.assigned=num_to_assign;
					u.original=b->maxUnitWorking;
					u.is_repair=false;
					pending_construction.push_back(u);
					ai.orders.push(new OrderConstruction(b->gid));
					ai.getUnitModule()->request("RandomUpgradeRepairModule", WORKER, BUILD, b->type->level+2, u.assigned, UnitModule::medium, b->gid);
					reduce(free_workers, b->type->level+1, num_to_assign);
					construction_counts[b->type->shortTypeNum]+=1;
				}
			}
	}
	return false;
}



DistributedUnitManager::DistributedUnitManager(AINicowar& ai) : ai(ai)
{

}

string DistributedUnitManager::getName() const
{
	return "DistributedUnitManager";
}

bool DistributedUnitManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("DistributedUnitManager");
	stream->readEnterSection("module_records");
	Uint32 unitRecordSize = stream->readUint32("size");
	for (Uint32 unitRecordIndex = 0; unitRecordIndex < unitRecordSize; unitRecordIndex++)
	{
		stream->readEnterSection(unitRecordIndex);
		moduleRecord mr;
		string name=stream->readText("name");
		for(unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; ++i)
		{
			stream->readEnterSection(i);
			for(unsigned int j=0; static_cast<int>(j)<NB_ABILITY; ++j)
			{
				stream->readEnterSection(j);
				for(unsigned int k=0; static_cast<int>(k)<NB_UNIT_LEVELS; ++k)
				{
					stream->readEnterSection(k);
					for(unsigned int l=0; l<priorityNum; ++l)
					{
						stream->readEnterSection(l);
						mr.requested[i][j][k][l]=stream->readUint32("requested");
						mr.usingUnits[i][j][k][l]=stream->readUint32("usingUnits");
						stream->readLeaveSection();
					}
					stream->readLeaveSection();
				}
				stream->readLeaveSection();
			}
			stream->readLeaveSection();
		}
		module_records[name]=mr;
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->readEnterSection("buildings");
	unitRecordSize = stream->readUint32("size");
	for (Uint32 unitRecordIndex = 0; unitRecordIndex < unitRecordSize; unitRecordIndex++)
	{
		stream->readEnterSection(unitRecordIndex);
		usageRecord ur;
		unsigned int gid=stream->readUint32("gid");
		ur.owner= stream->readText("owner");
		ur.x=stream->readUint32("x");
		ur.y=stream->readUint32("y");
		ur.type=stream->readUint32("type");
		ur.level=stream->readUint32("level");
		ur.ability=stream->readUint32("ability");
		ur.unit_type=stream->readUint32("unit_type");
		ur.minimum_level=stream->readUint32("minimum_level");
		ur.priority=static_cast<UnitModule::Priority>(stream->readUint32("priority"));
		buildings[gid]=ur;
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}

void DistributedUnitManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("DistributedUnitManager");
	stream->writeEnterSection("module_records");
	stream->writeUint32(module_records.size(), "size");
	Uint32 unitRecordIndex = 0;
	for (map<string, moduleRecord>::const_iterator i=module_records.begin(); i!=module_records.end(); ++i)
	{
		stream->writeEnterSection(unitRecordIndex++);
		stream->writeText(i->first, "name");
		for(unsigned int j=0; static_cast<int>(j)<NB_UNIT_TYPE; ++j)
		{
			stream->writeEnterSection(j);
			for(unsigned int k=0; static_cast<int>(k)<NB_ABILITY; ++k)
			{
				stream->writeEnterSection(k);
				for(unsigned int l=0; static_cast<int>(l)<NB_UNIT_LEVELS; ++l)
				{
					stream->writeEnterSection(l);
					for(unsigned int m=0; m<priorityNum; ++m)
					{
						stream->writeEnterSection(m);
						stream->writeUint32(i->second.requested[j][k][l][m], "requested");
						stream->writeUint32(i->second.usingUnits[j][k][l][m], "usingUnits");
						stream->writeLeaveSection();
					}
					stream->writeLeaveSection();
				}
				stream->writeLeaveSection();
			}
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->writeEnterSection("buildings");
	stream->writeUint32(buildings.size(), "size");
	unitRecordIndex = 0;
	for(map<int, usageRecord>::const_iterator i = buildings.begin(); i!=buildings.end(); ++i)
	{
		stream->writeEnterSection(unitRecordIndex++);
		stream->writeUint32(i->first, "gid");
		stream->writeText(i->second.owner, "owner");
		stream->writeUint32(i->second.x, "x");
		stream->writeUint32(i->second.y, "y");
		stream->writeUint32(i->second.type, "type");
		stream->writeUint32(i->second.level, "level");
		stream->writeUint32(i->second.ability, "ability");
		stream->writeUint32(i->second.unit_type, "unit_type");
		stream->writeUint32(i->second.minimum_level, "minimum_level");
		stream->writeUint32(i->second.priority, "priority");
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}


void DistributedUnitManager::changeUnits(string moduleName, unsigned int unitType, unsigned int numUnits, unsigned int ability, unsigned int level, Priority priority)
{
	level-=1;
//	std::cout<<"moduleName="<<moduleName<<"; unitType="<<unitType<<"; numUnits="<<numUnits<<"; ability="<<ability<<"; level="<<level<<";"<<endl;
	module_records[moduleName].requested[unitType][ability][level][priority]=numUnits;
}




unsigned int DistributedUnitManager::available(string module_name, unsigned int unit_type, unsigned int ability, unsigned int level, bool is_minimum, Priority priority)
{
//	if(level==0)
//		std::cout<<"Warning: available passed with 0 for minimum_level. module_name="<<module_name<<";"<<endl;
//	std::cout<<"module_name="<<module_name<<"; unit_type="<<unit_type<<"; ability="<<ability<<"; level="<<level<<"; is_minimum="<<is_minimum<<"; priority="<<priority<<";"<<endl;
	TeamStatsGenerator stat(ai.team);
	unsigned int num_available=stat.getUnits(unit_type, Unit::MED_FREE, Unit::ACT_RANDOM, ability, level, is_minimum);
//	std::cout<<"    num_available(1)="<<num_available<<";"<<endl;
	level-=1;
	for(map<int, usageRecord>::iterator i=buildings.begin(); i!=buildings.end(); ++i)
	{
		Building* b = getBuildingFromGid(ai.game, i->first);
		if(b==NULL ||  b->posX != static_cast<int>(i->second.x) || b->posY != static_cast<int>(i->second.y) || b->type->shortTypeNum != static_cast<int>(i->second.type) || b->type->level!=static_cast<int>(i->second.level))
		{
			buildings.erase(i);
			continue;
		}
		if((is_minimum && i->second.ability==ability && i->second.level>=level) || (!is_minimum && i->second.ability==ability && i->second.level==level))
		{
			unsigned int needed=b->maxUnitWorking-b->unitsWorking.size();
//			std::cout<<"needed="<<needed<<"; i->first="<<i->first<<"; b->type->shortTypeNum="<<b->type->shortTypeNum<<endl;
			if(needed>num_available)
			{
				num_available=0;
			}
			else
			{
				num_available-=needed;
			}
		}
	}
//	std::cout<<"    num_available(2)="<<num_available<<";"<<endl;

	///This is given as the 'unreachable' highest value.
	unsigned int min_percent=10000;
	string min_module="";
	unsigned int min_total_requested=0;
	unsigned int min_total_used=0;
	for(map<string, moduleRecord>::iterator i=module_records.begin(); i!=module_records.end(); ++i)
	{
		unsigned int total_requested=0;
		unsigned int total_used=0;
		//UnitModule::low is a special case, units made by it should be ignored
		for(unsigned int j=0; j<priorityNum && j!=UnitModule::low; ++j)
		{
				total_requested+=i->second.requested[unit_type][ability][level][j]*PRIORITY_SCORES[j];
				total_used+=i->second.usingUnits[unit_type][ability][level][j]*PRIORITY_SCORES[j];
				
		}
		if(total_requested==0)
			continue;
//		std::cout<<"        module_name="<<i->first<<"; total_requested="<<total_requested<<"; total_used="<<total_used<<";"<<endl;
		unsigned int percent=0;
		if(total_requested>0)
			percent=total_used*100/total_requested;
		else
			percent=100;
		if(percent<min_percent || (i->first==module_name && percent==min_percent))
		{
			min_percent=percent;
			min_module=i->first;
			min_total_requested=total_requested;
			min_total_used=total_used;
		}
	}

//	std::cout<<"    min_module="<<min_module<<";"<<endl;
//	std::cout<<"    min_percent="<<min_percent<<";"<<endl;
//	std::cout<<"    min_total_requested="<<min_total_requested<<";"<<endl;

	if(min_module!=module_name)
		return 0;

	return num_available;
}




bool DistributedUnitManager::request(string module_name, unsigned int unit_type, unsigned int ability, unsigned int minimum_level, unsigned int number, Priority priority, int building)
{
//	if(minimum_level==0)
//		std::cout<<"Warning: request passed with 0 for minimum_level. request(module_name="<<module_name<<", unit_type="<<unit_type<<", ability="<<ability<<", minimum_level="<<minimum_level<<", number="<<number<<", priority="<<priority<<")"<<endl;

	minimum_level-=1;
	usageRecord ur;
	Building* b=getBuildingFromGid(ai.game, building);
	if(buildings.find(building)!=buildings.end())
	{
		ur=buildings[building];
		module_records[ur.owner].usingUnits[ur.unit_type][ur.ability][ur.minimum_level][ur.priority]-=ur.number;
		if(b==NULL)
		{
			buildings.erase(buildings.find(building));
		}
	}
	if(b==NULL)
		return false;

	ur.owner=module_name;
	ur.x=b->posX;
	ur.y=b->posY;
	ur.type=b->type->shortTypeNum;
	ur.level=b->type->level;
	ur.ability=ability;
	ur.unit_type=unit_type;
	ur.minimum_level=minimum_level;
	ur.priority=priority;
	ur.number=number;
	buildings[building]=ur;
	module_records[ur.owner].usingUnits[ur.unit_type][ur.ability][ur.minimum_level][ur.priority]+=number;
	return true;
}




BasicDistributedSwarmManager::BasicDistributedSwarmManager(AINicowar& ai) : DistributedUnitManager(ai), ai(ai)
{
	ai.setUnitModule(this);
}




bool BasicDistributedSwarmManager::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return moderateSwarms();
	}
	return false;
}




string BasicDistributedSwarmManager::getName() const
{
	return "BasicDistributedSwarmManager";
}




bool BasicDistributedSwarmManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BasicDistributedSwarmManager");
	DistributedUnitManager::load(stream, player, versionMinor);
	stream->readLeaveSection();
	return true;
}




void BasicDistributedSwarmManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("BasicDistributedSwarmManager");
	DistributedUnitManager::save(stream);
	stream->writeLeaveSection();
}




bool BasicDistributedSwarmManager::moderateSwarms()
{
	//The number of units we want for each priority level
	unsigned int num_wanted[NB_UNIT_TYPE][priorityNum];
	unsigned int total_available[NB_UNIT_TYPE];
	for (unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; i++)
	{
		total_available[i]=ai.team->stats.getLatestStat()->numberUnitPerType[i];
		for(int n=0; n<priorityNum; ++n)
		{
			num_wanted[i][n]=0;
		}
	}

	//Counts out the requested units from each of the modules
	for (std::map<string, moduleRecord>::iterator i = module_records.begin(); i!=module_records.end(); i++)
	{
		for(unsigned int j=0; static_cast<int>(j)<NB_UNIT_TYPE; ++j)
			for(unsigned int k=0; static_cast<int>(k)<NB_ABILITY; ++k)
				for(unsigned int l=0; static_cast<int>(l)<NB_UNIT_LEVELS; ++l)
					for(unsigned int m=0; m<priorityNum; ++m)
						num_wanted[j][m]+=i->second.requested[j][k][l][m];
	}

	//Substract the already-existing amount of units from the numbers requested, and then move these totals multiplied by their respective score
	//into the ratios. ratios[NB_UNIT_TYPE] can't be unsigned or it won't pass into OrderModifySwarm properly
	int ratios[NB_UNIT_TYPE];
	unsigned int total_wanted_score=0;
	for (unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; i++)
	{
		ratios[i]=0;
		for(unsigned int n=0; n<priorityNum; ++n)
		{
			if(total_available[i] > num_wanted[i][n])
			{
				total_available[i]-=num_wanted[i][n];
				num_wanted[i][n]=0;
			}
			else
			{
				num_wanted[i][n]-=total_available[i];
			}
			ratios[i]+=num_wanted[i][n]*PRIORITY_SCORES[n];
		}
		total_wanted_score+=ratios[i];
	}

	int max=*max_element(ratios, ratios+NB_UNIT_TYPE);
	float devisor=1;
	if(max>16)
		devisor=static_cast<float>(max)/16.0;

	for(unsigned int i=0; static_cast<int>(i)<NB_UNIT_TYPE; ++i)
	{
		unsigned int num=ratios[i];
		ratios[i]=static_cast<int>(std::floor(num/devisor+0.5));
		if(num>0 && ratios[i]==0)
			ratios[i]=1;
	}

	unsigned int assigned_per_swarm=MAXIMUM_UNITS_FOR_SWARM;

	bool need_to_output=true;
	for (std::list<Building*>::iterator i = ai.team->swarms.begin(); i != ai.team->swarms.end(); ++i)
	{
		Building* swarm=*i;
		bool changed=false;
		for (int x = 0; x<NB_UNIT_TYPE; x++)
			if (swarm->ratio[x]!=ratios[x])
				changed=true;

		if(swarm->maxUnitWorking != static_cast<int>(assigned_per_swarm))
			ai.orders.push(new OrderModifyBuilding(swarm->gid, assigned_per_swarm));

		if(!changed)
			continue;

		if(AINicowar_DEBUG && need_to_output)
			std::cout<<"AINicowar: moderateSpawns: Turning changing production ratios on a swarm from {Worker:"<<swarm->ratio[0]<<", Explorer:"<<swarm->ratio[1]<<", Warrior:"<<swarm->ratio[2]<<"} to {Worker:"<<ratios[0]<<", Explorer:"<<ratios[1]<<", Warrior:"<<ratios[2]<<"}. Assigning "<<assigned_per_swarm<<" workers."<<endl;
		need_to_output=false;
		ai.orders.push(new OrderModifySwarm(swarm->gid, ratios));
	}
	return false;
}




ExplorationManager::ExplorationManager(AINicowar& ai) : ai(ai)
{
	ai.addOtherModule(this);
	explorers_wanted=0;
	developing_attack_explorers=false;
	explorer_attacking=false;
}




bool ExplorationManager::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return exploreWorld();
		case 1:
			return updateExplorationFlags();
		case 2:
			return moderateSwarmsForExplorers();
		case 3:
			return explorerAttack();
	}
	return false;
}




string ExplorationManager::getName() const
{
	return "ExplorationManager";
}




bool ExplorationManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ExplorationManager");
	stream->readEnterSection("active_exploration");
	Uint32 explorationRecordSize = stream->readUint32("size");
	for (Uint32 explorationRecordIndex = 0; explorationRecordIndex < explorationRecordSize; explorationRecordIndex++)
	{
		stream->readEnterSection(explorationRecordIndex);
		explorationRecord er;
		unsigned int gid=stream->readUint32("gid");
		if(!gid)
			er.flag=NULL;
		else
			er.flag=getBuildingFromGid(ai.game, gid);
		er.flag_x=stream->readUint32("flag_x");
		er.flag_y=stream->readUint32("flag_y");
		er.zone_x=stream->readUint32("zone_x");
		er.zone_y=stream->readUint32("zone_y");
		er.width=stream->readUint32("width");
		er.height=stream->readUint32("height");
		er.assigned=stream->readUint32("assigned");
		er.radius=stream->readUint32("radius");
		er.isAssaultFlag=stream->readUint8("isAssaultFlag");
		active_exploration.push_back(er);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	explorers_wanted=stream->readUint32("explorers_wanted");
	developing_attack_explorers=stream->readUint8("developing_attack_explorers");
	explorer_attacking=stream->readUint8("explorer_attacking");
	stream->readLeaveSection();
	return true;
}




void ExplorationManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("ExplorationManager");
	stream->writeEnterSection("active_exploration");
	stream->writeUint32(active_exploration.size(), "size");
	Uint32 explorationRecordIndex = 0;
	for(std::list<explorationRecord>::const_iterator i = active_exploration.begin(); i!=active_exploration.end(); ++i)
	{
		stream->writeEnterSection(explorationRecordIndex++);
		if(i->flag!=NULL)
			stream->writeUint32(i->flag->gid, "gid");
		else
			stream->writeUint32(0, "gid");
		stream->writeUint32(i->flag_x, "flag_x");
		stream->writeUint32(i->flag_y, "flag_y");
		stream->writeUint32(i->zone_x, "zone_x");
		stream->writeUint32(i->zone_y, "zone_y");
		stream->writeUint32(i->width, "width");
		stream->writeUint32(i->height, "height");
		stream->writeUint32(i->assigned, "assigned");
		stream->writeUint32(i->radius, "radius");
		stream->writeUint8(i->isAssaultFlag, "isAssaultFlag");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeUint32(explorers_wanted, "explorers_wanted");
	stream->writeUint8(developing_attack_explorers, "developing_attack_explorers");
	stream->writeUint8(explorer_attacking, "explorer_attacking");
	stream->writeLeaveSection();
}




bool ExplorationManager::exploreWorld(void)
{
	GridPollingSystem gps(ai);
	unsigned int exploration_count=0;
	unsigned int attack_count=0;
	for (std::list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end(); ++i)
	{

		if(i->isAssaultFlag)
			attack_count++;
		else
			exploration_count++;
	}

	int wanted_exploration=EXPLORER_MAX_REGIONS_AT_ONCE-exploration_count;
	int wanted_attack=EXPLORER_ATTACKS_AT_ONCE-attack_count;
	if(! explorer_attacking )
		wanted_attack=0;

	if(wanted_exploration==0 && wanted_attack==0)
		return false;

	GridPollingSystem::poll p;
	p.mod_1=GridPollingSystem::MAXIMUM;
	p.type_1=GridPollingSystem::HIDDEN_SQUARES;
	p.mod_2=GridPollingSystem::MINIMUM;
	p.type_2=GridPollingSystem::CENTER_DISTANCE;
	p.mod_minimum=GridPollingSystem::MAXIMUM;
	p.minimum_type=GridPollingSystem::VISIBLE_SQUARES;
	p.minimum_score=1;
	GridPollingSystem::getBestZonesSplit* split = gps.getBestZones(p, EXPLORER_REGION_WIDTH, EXPLORER_REGION_HEIGHT, EXPLORER_REGION_HORIZONTAL_OVERLAP,
		EXPLORER_REGION_VERTICAL_OVERLAP, EXPLORER_REGION_HORIZONTAL_EXTENTION,
		EXPLORER_REGION_VERTICAL_EXTENTION);
	vector<GridPollingSystem::zone> best = gps.getBestZones(split);
	unsigned int size=best.size();

	p.mod_1=GridPollingSystem::MAXIMUM;
	p.type_1=GridPollingSystem::ENEMY_BUILDINGS;
	p.mod_2=GridPollingSystem::MAXIMUM;
	p.type_2=GridPollingSystem::NONE;
	p.mod_minimum=GridPollingSystem::MAXIMUM;
	p.minimum_type=GridPollingSystem::ENEMY_BUILDINGS;
	p.minimum_score=1;
	split = gps.getBestZones(p, EXPLORER_ATTACK_AREA_WIDTH, EXPLORER_ATTACK_AREA_HEIGHT, EXPLORER_ATTACK_AREA_HORIZONTAL_OVERLAP,
		EXPLORER_ATTACK_AREA_VERTICAL_OVERLAP, EXPLORER_ATTACK_AREA_HORIZONTAL_EXTENTION,
		EXPLORER_ATTACK_AREA_VERTICAL_EXTENTION);

	vector<GridPollingSystem::zone> best_attack = gps.getBestZones(split);

	std::copy(best_attack.begin(), best_attack.end(), back_insert_iterator<vector<GridPollingSystem::zone> >(best));

	vector<GridPollingSystem::zone>::iterator attack_start=best.begin()+size;

	///Erase any zones that already have flags in them
	for(vector<GridPollingSystem::zone>::iterator i = best.begin(); i!=best.end();)
	{
		if(!isFreeOfFlags(i->x+i->width/2, i->y+i->height/2))
		{
			//			std::cout<<"removing zone from list!"<<endl;
			i = best.erase(i);
		}
		else
		{
			++i;
		}
	}

	vector<GridPollingSystem::zone>::iterator top=best.begin();
	if(top<attack_start && gps.pollArea(top->x, top->y, top->width, top->height, GridPollingSystem::MAXIMUM, GridPollingSystem::HIDDEN_SQUARES)==0)
	{
		//		std::cout<<"Doing nothing! wanted_exploration="<<wanted_exploration<<";"<<endl;
		return false;
	}

	for (vector<GridPollingSystem::zone>::iterator i=best.begin(); i!=best.end(); ++i)
	{
		//		std::cout<<"wanted_exploration="<<wanted_exploration<<"; "<<endl;
		GridPollingSystem::zone z = *i;
		if((i>=attack_start && wanted_attack==0) || (wanted_attack==0 && wanted_exploration==0))
			break;

		if(wanted_exploration==0)
			continue;

		explorationRecord exploration_record;
		//This flag will be picked up by findCreatedFlags
		exploration_record.flag=NULL;
		exploration_record.flag_x=z.x+(z.width/2);
		if(static_cast<int>(exploration_record.flag_x)>=ai.map->getW())
			exploration_record.flag_x-=ai.map->getW();
		exploration_record.flag_y=z.y+(z.height/2);
		if(static_cast<int>(exploration_record.flag_y)>=ai.map->getH())
			exploration_record.flag_y-=ai.map->getH();
		exploration_record.zone_x=z.x;
		exploration_record.zone_y=z.y;
		exploration_record.width=z.width;
		exploration_record.height=z.height;
		if(i<attack_start)
		{
			exploration_record.assigned=EXPLORERS_PER_REGION;
			exploration_record.radius=EXPLORATION_FLAG_RADIUS;
			exploration_record.isAssaultFlag=false;
			wanted_exploration--;
		}
		else
		{
			exploration_record.assigned=EXPLORERS_PER_ATTACK;
			exploration_record.radius=EXPLORATION_FLAG_ATTACK_RADIUS;
			exploration_record.isAssaultFlag=true;
			wanted_attack--;
		}

		active_exploration.push_back(exploration_record);

		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum("explorationflag", 0, false);
		if(AINicowar_DEBUG)
			std::cout<<"AINicowar: exploreWorld: Creating a new flag at position "<<exploration_record.flag_x<<","<<exploration_record.flag_y<<"."<<endl;
		ai.orders.push(new OrderCreate(ai.team->teamNumber, exploration_record.flag_x, exploration_record.flag_y,typeNum));
		explorers_wanted+=exploration_record.assigned;
	}
	return false;
}




bool ExplorationManager::updateExplorationFlags(void)
{
	GridPollingSystem gps(ai);
	//Iterate through the buildings (which includes flags), looking for a flag that is not on the lists, that is in the right
	//spot to be one of our null records flags.
	Building** myBuildings=ai.team->myBuildings;
	for (int i=0; i<1024; ++i)
	{
		Building* b=myBuildings[i];
		if (b)
		{
			if (b->type->shortTypeNum==IntBuildingType::EXPLORATION_FLAG)
			{
				for (list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end(); ++i)
				{
					if (i->flag==NULL && b->posX==static_cast<int>(i->flag_x) && b->posY==static_cast<int>(i->flag_y) && b->type->shortTypeNum==IntBuildingType::EXPLORATION_FLAG)
					{
						i->flag=b;
						if(AINicowar_DEBUG)
							std::cout<<"AINicowar: updateExplorationFlags: Found a newly created flag matching our records at positon "<<b->posX<<","<<b->posY<<" , inserting this flag into our records."<<endl;
						ai.orders.push(new OrderModifyFlag(b->gid, i->radius));
						ai.orders.push(new OrderModifyBuilding(b->gid, i->assigned));
						ai.getUnitModule()->request("ExplorationManager", EXPLORER, FLY, 1, i->assigned, UnitModule::medium_high, i->flag->gid);
						break;
					}
				}
			}
		}
	}

	for (std::list<explorationRecord>::iterator i = active_exploration.begin(); i!=active_exploration.end();)
	{
		if((!i->isAssaultFlag || explorer_attacking) && i->flag!=NULL)
		{
			int score=0;
			if(i->isAssaultFlag)
				score=gps.pollArea(i->zone_x, i->zone_y, i->width, i->height, GridPollingSystem::MAXIMUM, GridPollingSystem::ENEMY_BUILDINGS);
			else
				score=gps.pollArea(i->zone_x, i->zone_y, i->width, i->height, GridPollingSystem::MAXIMUM, GridPollingSystem::HIDDEN_SQUARES);

			if(score==0)
			{
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: updateExplorationFlags: Removing a flag this is no longer needed at "<<i->flag_x<<", "<<i->flag_y<<"."<<endl;
				ai.orders.push(new OrderDelete(i->flag->gid));
				explorers_wanted-=i->assigned;
				i=active_exploration.erase(i);
			}
			else
				++i;
		}
		else
			++i;
	}

	return false;
}




bool ExplorationManager::isFreeOfFlags(unsigned int x, unsigned int y)
{
	//Check if theres any flags of other types on the spot.
	Building** myBuildings=ai.team->myBuildings;
	for (int i=0; i<1024; i+=1)
	{
		Building *b=myBuildings[i];
		if (b)
		{
			if (    b->type->shortTypeNum==IntBuildingType::EXPLORATION_FLAG ||
				b->type->shortTypeNum==IntBuildingType::WAR_FLAG ||
				b->type->shortTypeNum==IntBuildingType::CLEARING_FLAG)
			{
				if (b->posX == static_cast<int>(x) && b->posY == static_cast<int>(y))
				{
					return false;
				}
			}
		}
	}

	return true;
}




bool ExplorationManager::moderateSwarmsForExplorers(void)
{
	//I've raised the priority on explorers temporarily for testing.
	//	changeUnits("aircontrol", EXPLORER, static_cast<int>(desired_explorers/2) , desired_explorers, 0);
	ai.getUnitModule()->changeUnits("ExplorationManager", EXPLORER, explorers_wanted, FLY, 1, UnitModule::medium_high);
	return false;
}




bool ExplorationManager::explorerAttack(void)
{
	bool lvl3_school_exists=false;
	Building** myBuildings=ai.team->myBuildings;
	for (int i=0; i<1024; ++i)
	{
		Building* b=myBuildings[i];
		if (b)
		{
			if (b->type->shortTypeNum==IntBuildingType::SCIENCE_BUILDING && b->type->level==2)
			{
				lvl3_school_exists=true;
			}
		}
	}
	if (!lvl3_school_exists)
	{
		return false;
	}

	if (!developing_attack_explorers)
	{
		developing_attack_explorers=true;
		return false;
	}

	//check if we have enough explorers to start launching attacks.
	int ground_attack_explorers=0;
	Unit** myUnits = ai.team->myUnits;
	for (int i=0; i<1024; i++)
	{
		Unit* u = myUnits[i];
		if (u)
		{
			if (u->canLearn[MAGIC_ATTACK_GROUND] && u->performance[MAGIC_ATTACK_GROUND])
			{
				ground_attack_explorers+=1;
			}
		}
	}

	if (ground_attack_explorers<static_cast<int>(EXPLORERS_PER_ATTACK*EXPLORER_ATTACKS_AT_ONCE))
		return false;
	if(AINicowar_DEBUG)
		if(!explorer_attacking)
			std::cout<<"AINicowar: explorerAttack: Enabling explorer attacks."<<endl;
	explorer_attacking=true;
	return false;
}




InnManager::InnManager(AINicowar& ai) : ai(ai)
{
	ai.addOtherModule(this);
}




bool InnManager::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return recordInns();
		case 1:
			return modifyInns();
	}
	return false;
}




string InnManager::getName() const
{
	return "InnManager";
}




bool InnManager::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("InnManager");
	stream->readEnterSection("inns");
	Uint32 innRecordSize = stream->readUint32("size");
	for (Uint32 innRecordIndex = 0; innRecordIndex < innRecordSize; innRecordIndex++)
	{
		stream->readEnterSection(innRecordIndex);
		innRecord ir;
		unsigned int gid=stream->readUint32("gid");
		ir.pos=stream->readUint32("pos");
		unsigned int size=stream->readUint32("size");
		for(unsigned int i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			ir.records[i].food_amount=stream->readUint32("food_amount");
			stream->readLeaveSection();
		}
		inns[gid]=ir;
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
	return true;
}




void InnManager::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("InnManager");
	stream->writeEnterSection("inns");
	stream->writeUint32(static_cast<Uint32>(inns.size()), "size");
	Uint32 innRecordIndex = 0;
	for(std::map<int, innRecord>::const_iterator i = inns.begin(); i!=inns.end(); ++i)
	{
		stream->writeEnterSection(innRecordIndex++);
		stream->writeUint32(i->first, "gid");
		stream->writeUint32(i->second.pos, "pos");
		stream->writeUint32(i->second.records.size(), "size");
		Uint32 recordIndex = 0;
		for(std::vector<singleInnRecord>::const_iterator record = i->second.records.begin(); record!=i->second.records.end(); ++record)
		{
			stream->writeEnterSection(recordIndex++);
			stream->writeUint32(record->food_amount, "food_amount");
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}




InnManager::innRecord::innRecord() : pos(0), records() {}

bool InnManager::recordInns()
{
	for(int i=0; i<1024; ++i)
	{
		Building* b=ai.team->myBuildings[i];
		if (b)
		{
			if(b->type->shortTypeNum==IntBuildingType::FOOD_BUILDING && b->constructionResultState==Building::NO_CONSTRUCTION)
			{
				innRecord& i = inns[b->gid];
				if(i.records.size()<INN_RECORD_MAX)
					i.records.push_back(singleInnRecord(b->ressources[CORN]));
				else
					i.records[i.pos].food_amount=b->ressources[CORN];
				i.pos+=1;
				if (i.pos==INN_RECORD_MAX)
				{
					i.pos=0;
				}
			}
		}
	}
	return false;
}




bool InnManager::modifyInns()
{
	unsigned int total_workers_needed=0;

	for(std::map<int, innRecord>::iterator i = inns.begin(); i!=inns.end(); ++i)
	{
		Building* inn=getBuildingFromGid(ai.game, i->first);
		if (inn==NULL)
		{
			inns.erase(i);
			continue;
		}

		if (inn->constructionResultState!=Building::NO_CONSTRUCTION)
		{
			continue;
		}

		unsigned int average=0;
		for (vector<singleInnRecord>::iterator record = i->second.records.begin(); record!=i->second.records.end(); ++record)
		{
			average+=record->food_amount;
		}
		average/=i->second.records.size();


		unsigned int to_assign=std::max(INN_MINIMUM[inn->type->level], std::min(INN_MAX[inn->type->level], (inn->type->maxRessource[CORN]-average)/WHEAT_NEEDED_FOR_UNIT));
		total_workers_needed+=to_assign;
		if(static_cast<int>(to_assign)!=inn->maxUnitWorking)
		{
			if(AINicowar_DEBUG)
				std::cout<<"AINicowar: modifyInns: Changing the number of units assigned to an inn from "<<inn->maxUnitWorking<<" to "<<to_assign<<"."<<endl;
			ai.orders.push(new OrderModifyBuilding(inn->gid, to_assign));
			ai.getUnitModule()->request("InnManager", WORKER, HARVEST, 1, to_assign, UnitModule::medium, inn->gid);
		}
	}

	ai.getUnitModule()->changeUnits("InnManager", WORKER, total_workers_needed, HARVEST, 1, UnitModule::medium);
	return false;
}




TowerController::TowerController(AINicowar& ai) : ai(ai)
{
	ai.addOtherModule(this);
}




bool TowerController::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return controlTowers();
	}
	return false;
}




string TowerController::getName() const
{
	return "TowerController";
}




bool TowerController::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("TowerController");
	stream->readLeaveSection();
	return true;
}




void TowerController::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("TowerController");
	stream->writeLeaveSection();
}




bool TowerController::controlTowers()
{

	int count=0;
	for(int i=0; i<1024; i++)
	{
		Building* b = ai.team->myBuildings[i];
		if (b)
		{
			if(b->type->shortTypeNum == IntBuildingType::DEFENSE_BUILDING &&
				b->buildingState==Building::ALIVE &&
				b->constructionResultState==Building::NO_CONSTRUCTION)
			{
				count+=1;
				if(b->maxUnitWorking != static_cast<int>(NUM_PER_TOWER))
				{
					if(AINicowar_DEBUG)
						std::cout<<"AINicowar: controlTowers: Changing number of units assigned to a tower, from "<<b->maxUnitWorking<<" to "<<NUM_PER_TOWER<<"."<<endl;
					ai.orders.push(new OrderModifyBuilding(b->gid, NUM_PER_TOWER));
				}
			}
		}
	}
	ai.getUnitModule()->changeUnits("TowerController", WORKER, count*NUM_PER_TOWER, HARVEST, 1, UnitModule::low);

	return false;
}




BuildingClearer::BuildingClearer(AINicowar& ai) : ai(ai)
{
	ai.addOtherModule(this);
}




bool BuildingClearer::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return removeOldPadding();
		case 1:
			return updateClearingAreas();
	}
	return false;
}




string BuildingClearer::getName() const
{
	return "BuildingClearer";
}




bool BuildingClearer::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("BuildingClearer");
	Uint32 clearingRecordSize = stream->readUint32("size");
	for (Uint32 clearingRecordIndex = 0; clearingRecordIndex < clearingRecordSize; clearingRecordIndex++)
	{
		stream->readEnterSection(clearingRecordIndex);
		clearingRecord cr;
		cr.x=stream->readUint32("x");
		cr.y=stream->readUint32("y");
		cr.width=stream->readUint32("width");
		cr.height=stream->readUint32("height");
		cr.level=stream->readUint32("level");
		cleared_buildings[stream->readUint32("first")]=cr;
		// FIXME : clear the container before load

		stream->readLeaveSection();
	}

	stream->readLeaveSection();
	return true;
}




void BuildingClearer::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("BuildingClearer");
	stream->writeUint32(cleared_buildings.size(), "size");
	Uint32 clearingRecordIndex = 0;
	for(map<int, clearingRecord>::const_iterator i=cleared_buildings.begin(); i!=cleared_buildings.end(); ++i)
	{
		stream->writeEnterSection(clearingRecordIndex++);
		stream->writeUint32(i->second.x, "x");
		stream->writeUint32(i->second.y, "y");
		stream->writeUint32(i->second.width, "width");
		stream->writeUint32(i->second.height, "height");
		stream->writeUint32(i->second.level, "level");
		stream->writeUint32(i->first, "first");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}




bool BuildingClearer::removeOldPadding()
{
	for(map<int, clearingRecord>::iterator i=cleared_buildings.begin(); i!=cleared_buildings.end(); ++i)
	{
		Building* b = getBuildingFromGid(ai.game, i->first);
		if(b==NULL || b->type->level != static_cast<int>(i->second.level) || i->second.x!=b->posX-CLEARING_AREA_BUILDING_PADDING || i->second.y!=b->posY-CLEARING_AREA_BUILDING_PADDING)
		{

			unsigned int x_bound=i->second.x+i->second.width;
			unsigned int y_bound=i->second.y+i->second.height;
			if(static_cast<int>(x_bound) > ai.map->getW())
				x_bound-=ai.map->getW();
			if(static_cast<int>(y_bound) > ai.map->getH())
				y_bound-=ai.map->getH();

			BrushAccumulator acc;
			for(unsigned int x=i->second.x; x!=x_bound; ++x)
			{
				if(static_cast<int>(x)==ai.map->getW())
					x=0;
				for(unsigned int y=i->second.y; y!=y_bound; ++y)
				{
					if(static_cast<int>(y)==ai.map->getH())
						y=0;
					if(ai.map->isClearAreaLocal(x,y))
					{
						acc.applyBrush(ai.map, BrushApplication(x,y,0));
					}
				}
			}
			if(acc.getApplicationCount()>0)
				ai.orders.push(new OrderAlterateClearArea(ai.team->teamNumber, BrushTool::MODE_DEL, &acc));
			cleared_buildings.erase(i);
		}
	}
	return false;
}




bool BuildingClearer::updateClearingAreas()
{
	for(unsigned int i=0; i<1024; ++i)
	{
		Building* b = ai.team->myBuildings[i];
		if(b)
		{
			if( b->type->shortTypeNum!=IntBuildingType::EXPLORATION_FLAG &&
				b->type->shortTypeNum!=IntBuildingType::WAR_FLAG &&
				b->type->shortTypeNum!=IntBuildingType::CLEARING_FLAG &&
				cleared_buildings.find(b->gid) == cleared_buildings.end())
			{
				clearingRecord cr;
				cr.x=b->posX-CLEARING_AREA_BUILDING_PADDING;
				cr.y=b->posY-CLEARING_AREA_BUILDING_PADDING;
				cr.width=b->type->width+CLEARING_AREA_BUILDING_PADDING*2;
				cr.height=b->type->height+CLEARING_AREA_BUILDING_PADDING*2;
				cr.level=b->type->level;

				unsigned int x_bound=cr.x+cr.width;
				unsigned int y_bound=cr.y+cr.height;
				if(static_cast<int>(x_bound) > ai.map->getW())
					x_bound-=ai.map->getW();
				if(static_cast<int>(y_bound) > ai.map->getH())
					y_bound-=ai.map->getH();

				BrushAccumulator acc;
				for(unsigned int x=cr.x; x!=x_bound; ++x)
				{
					if(static_cast<int>(x)==ai.map->getW())
						x=0;
					for(unsigned int y=cr.y; y!=y_bound; ++y)
					{
						if(static_cast<int>(y)==ai.map->getH())
							y=0;
						if(!ai.map->isClearAreaLocal(x,y))
						{
							acc.applyBrush(ai.map, BrushApplication(x, y, 0));
						}
					}
				}
				if(AINicowar_DEBUG)
					std::cout<<"AINicowar: updateClearingAreas: Adding clearing area around the building at "<<b->posX<<","<<b->posY<<"."<<endl;
				if(acc.getApplicationCount()>0)
					ai.orders.push(new OrderAlterateClearArea(ai.team->teamNumber, BrushTool::MODE_ADD, &acc));
				cleared_buildings[b->gid]=cr;
			}
		}
	}
	return false;
}




HappinessHandler::HappinessHandler(AINicowar& ai) : ai(ai)
{
	ai.addOtherModule(this);
}




HappinessHandler::~HappinessHandler()
{

}




bool HappinessHandler::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return adjustAlliances();
	}

	return false;
}




string HappinessHandler::getName() const
{
	return "HappinessHandler";
}




bool HappinessHandler::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("HappinessHandler");
	stream->readLeaveSection();
	return true;
}




void HappinessHandler::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("HappinessHandler");
	stream->writeLeaveSection();
}




bool HappinessHandler::adjustAlliances()
{
	Uint32 food_mask=ai.team->me;
	unsigned int total_happiness=0;
	for(unsigned int i=0; i<HAPPYNESS_COUNT+1; ++i)
	{
		total_happiness+=ai.team->stats.getLatestStat()->happiness[i]*i;
	}

	for(unsigned int i=0;  i<32; ++i)
	{
		Team* t=ai.game->teams[i];
		if(t)
		{
			if(t->me & ai.team->enemies)
			{
				unsigned int enemy_happiness=0;
				for(unsigned int i=0; i<HAPPYNESS_COUNT+1; ++i)
				{
					enemy_happiness+=t->stats.getLatestStat()->happiness[i]*i;
				}
				if(total_happiness>enemy_happiness)
				{
					food_mask=food_mask|t->me;
				}
			}
		}
	}

	if(food_mask!=ai.team->sharedVisionFood)
	{
		if(AINicowar_DEBUG)
			std::cout<<"AINicowar: adjustAlliances: Adjusting food vision alliance."<<endl;
		ai.orders.push(new SetAllianceOrder(ai.team->teamNumber, ai.team->allies, ai.team->enemies, ai.team->sharedVisionExchange, food_mask, ai.team->sharedVisionOther));
	}

	return false;
}




Farmer::Farmer(AINicowar& ai) : ai(ai)
{
	ai.addOtherModule(this);
}




Farmer::~Farmer()
{

}




bool Farmer::perform(unsigned int time_slice_n)
{
	switch(time_slice_n)
	{
		case 0:
			return updateFarm();

	}
	return false;
}




string Farmer::getName() const
{
	return "Farmer";
}




bool Farmer::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Farmer");
	Uint32 pointsSize = stream->readUint32("size");
	for (Uint32 pointsIndex = 0; pointsIndex < pointsSize; pointsIndex++)
	{
		stream->readEnterSection(pointsIndex);
		point p;
		p.x=stream->readUint16("x");
		p.y=stream->readUint16("y");
		resources.insert(p);
		// FIXME : clear the container before load
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	return true;
}




void Farmer::save(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("Farmer");
	stream->writeUint32(resources.size(), "size");
	Uint32 pointsIndex = 0;
	for(set<point>::const_iterator i=resources.begin(); i!=resources.end(); ++i)
	{
		stream->writeEnterSection(pointsIndex++);
		stream->writeUint16(i->x, "x");
		stream->writeUint16(i->y, "y");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
}




bool Farmer::updateFarm()
{
	BrushAccumulator del_acc;
	BrushAccumulator add_acc;
	for(unsigned int x=0; static_cast<int>(x)<ai.map->getW(); ++x)
	{
		for(unsigned int y=0; static_cast<int>(y)<ai.map->getH(); ++y)
		{

			if( (x%2!=y%2) && FARMING_METHOD==CheckerBoard ||
				(x%2==1 && y%2==1) && FARMING_METHOD==CrossSpacing ||
				(x%6<4 && y%3==0) && FARMING_METHOD==Row4 ||
				(x%3==0 && y%6<4) && FARMING_METHOD==Column4)
			{
				if((!ai.map->isRessourceTakeable(x, y, WOOD) && !ai.map->isRessourceTakeable(x, y, CORN)) || ai.map->isClearAreaLocal(x, y))
				{
					if(resources.find(point(x, y))!=resources.end())
					{
						del_acc.applyBrush(ai.map, BrushApplication(x, y, 0));
						resources.erase(resources.find(point(x, y)));
					}
				}
				else
				{
					if(resources.find(point(x, y))==resources.end() && ai.map->isMapDiscovered(x, y, ai.team->me))
					{
						add_acc.applyBrush(ai.map, BrushApplication(x, y, 0));
						resources.insert(point(x, y));
					}
				}
			}
		}
	}

	if(del_acc.getApplicationCount()>0)
		ai.orders.push(new OrderAlterateForbidden(ai.team->teamNumber, BrushTool::MODE_DEL, &del_acc));
	if(add_acc.getApplicationCount()>0)
		ai.orders.push(new OrderAlterateForbidden(ai.team->teamNumber, BrushTool::MODE_ADD, &add_acc));
	return false;
}
