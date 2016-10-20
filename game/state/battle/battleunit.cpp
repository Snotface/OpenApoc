#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "game/state/battle/battleunit.h"
#include "framework/framework.h"
#include "framework/sound.h"
#include "game/state/aequipment.h"
#include "game/state/battle/battle.h"
#include "game/state/battle/battleitem.h"
#include "game/state/battle/battleunitanimationpack.h"
#include "game/state/city/projectile.h"
#include "game/state/gamestate.h"
#include "game/state/rules/damage.h"
#include "game/state/tileview/collision.h"
#include "game/state/tileview/collision.h"
#include "game/state/tileview/tileobject_battleunit.h"
#include "game/state/tileview/tileobject_shadow.h"
#include "library/strings_format.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtx/vector_angle.hpp>

namespace OpenApoc
{

template <> sp<BattleUnit> StateObject<BattleUnit>::get(const GameState &state, const UString &id)
{
	auto it = state.current_battle->units.find(id);
	if (it == state.current_battle->units.end())
	{
		LogError("No agent_type matching ID \"%s\"", id.cStr());
		return nullptr;
	}
	return it->second;
}

template <> const UString &StateObject<BattleUnit>::getPrefix()
{
	static UString prefix = "BATTLEUNIT_";
	return prefix;
}
template <> const UString &StateObject<BattleUnit>::getTypeName()
{
	static UString name = "BattleUnit";
	return name;
}
template <>
const UString &StateObject<BattleUnit>::getId(const GameState &state, const sp<BattleUnit> ptr)
{
	static const UString emptyString = "";
	for (auto &a : state.current_battle->units)
	{
		if (a.second == ptr)
			return a.first;
	}
	LogError("No battleUnit matching pointer %p", ptr.get());
	return emptyString;
}

void BattleUnit::removeFromSquad(Battle &battle)
{
	battle.forces[owner].removeAt(squadNumber, squadPosition);
}

bool BattleUnit::assignToSquad(Battle &battle, int squad)
{
	return battle.forces[owner].insert(squad, shared_from_this());
}

void BattleUnit::moveToSquadPosition(Battle &battle, int position)
{
	battle.forces[owner].insertAt(squadNumber, position, shared_from_this());
}

bool BattleUnit::isFatallyWounded()
{
	for (auto e : fatalWounds)
	{
		if (e.second > 0)
		{
			return true;
		}
	}
	return false;
}

void BattleUnit::setPosition(GameState &state, const Vec3<float> &pos)
{
	bool unitChangedTiles = (Vec3<int>)pos != (Vec3<int>)position;
	position = pos;
	if (!tileObject)
	{
		LogError("setPosition called on unit with no tile object");
		return;
	}

	tileObject->setPosition(pos);

	if (shadowObject)
	{
		shadowObject->setPosition(tileObject->getCenter());
	}
	if (unitChangedTiles)
	{
		updateUnitVisibilityAndVision(state);
	}
}

void BattleUnit::updateUnitVisibility(GameState &state)
{
	// Update other units's vision of this unit

	StateRef<BattleUnit> thisUnit = {&state, id};
	for (auto u : state.current_battle->units)
	{
		if (u.second->owner == owner)
		{
			continue;
		}
		if (u.second->visibleUnits.find(thisUnit) != u.second->visibleUnits.end())
		{
			// FIXME: This is lazy, do it proper?
			u.second->updateUnitVision(state);
		}
	}
}

void BattleUnit::updateUnitVision(GameState &state)
{
	static const std::set<TileObject::Type> mapPartSet = {
	    TileObject::Type::Ground, TileObject::Type::LeftWall, TileObject::Type::RightWall,
	    TileObject::Type::Feature};

	auto &battle = *state.current_battle;
	auto &map = *battle.map;
	auto lastVisibleUnits = visibleUnits;
	visibleUnits.clear();

	// Vision is actually updated only if conscious, otherwise we clear visible units and that's it
	if (isConscious())
	{

		// Update unit's vision of los blocks
		auto idx = battle.getLosBlockID(position.x, position.y, position.z);
		if (!battle.visibleBlocks.at(owner).at(idx))
		{
			battle.visibleBlocks.at(owner).at(idx) = true;
			auto l = battle.los_blocks.at(idx);
			for (int x = l->start.x; x < l->end.x; x++)
			{
				for (int y = l->start.y; y < l->end.y; y++)
				{
					for (int z = l->start.z; z < l->end.z; z++)
					{
						battle.setVisible(owner, x, y, z);
					}
				}
			}
		}

		// Update unit's vision of terrain
		// Update unit's vision of other units

		// Algorithm:
		//
		// This is UFO EU vision algorithm, I assume Apoc does the same (or similar)
		// FOV is 90 degrees and vision deteriorates 1 tile forward per 2 tiles to the side
		// Which means, unit can see 20 tiles forward, or 19 tiles forward +-2, or 18 tiles forward
		// +-4
		// Two lines formed by this formula reach Pure diagonal at 13
		//
		// Reference links:
		// http://www.ufopaedia.org/index.php/Line_of_sight
		// http://www.ufopaedia.org/index.php/File:VizRange20.gif
		//
		// If unit is looking N/S/E/W, algorithm is simple.
		// Let axis unit is facing on be A1 (X if E/W, Y if N/S), and other axis be A2.
		// Let coordinates on these axes be called C1 and C2.
		// C2 goes from -13 to +13 C1 is calculated using formula C1 = 20 - (|C2| + 1) / 2
		// We then apply sign: if unit is facing towards +inf on A1, sign is "+", else sign is "-"
		// This way, we sweep the 90 degree arc.
		//
		// If unit is looking diagonally, algorithm is more complicated.
		// We do the same as above, but we must flip axes for a half of the arc.
		// We must flip signs too. This is done after we process the middle value.

		auto eyesPos = getMuzzleLocation();
		bool diagonal = facing.x != 0 && facing.y != 0;
		bool swap = facing.x == 0;
		bool inverseC1 = false; // changed halfway when processing diagonals
		int signC2 = (diagonal && facing.y > 0) ? -1 : 1;
		int signC1 = (swap && facing.y < 0) || (!swap && facing.x < 0) ? -1 : 1;

		for (int i = -13; i < 14; i++)
		{
			int c2 = inverseC1 ? 1 - i : i;
			int c1 = 20 - (std::abs(c2) + 1) / 2;
			int x = position.x + (swap ? c2 * signC2 : c1 * signC1);
			int y = position.y + (swap ? c1 * signC1 : c2 * signC2);

			if (i == 0 && diagonal)
			{
				swap = !swap;
				int sC1 = signC1;
				signC1 = -signC2;
				signC2 = -sC1;
				inverseC1 = true;
			}

			for (int z = 0; z < battle.size.z; z++)
			{
				auto c = map.findCollision(eyesPos, {x + 0.5f, y + 0.5f, z + 0.5f}, mapPartSet,
				                           tileObject, true, false, true);
				if (c)
				{
					// We ignore wall/ground if we come from outside the tile
					auto t = c.obj->getType();
					// FIXME: This does not work as intended. Need improvement
					// Sometimes collision will happen with the feature instead of ground/wall
					// This allows vision into tiles that should otherwise be concealed.
					if ((t == TileObject::Type::Ground && z > position.z) ||
					    (t == TileObject::Type::LeftWall && x > position.x) ||
					    (t == TileObject::Type::RightWall && y > position.y))
					{
						c.tilesPassed.pop_back();
					}
				}
				// Apply vision blockage
				// We apply a median value accumulated in all tiles passed every time we pass a tile
				// This makes it so that we do not over or under-apply smoke when going diagonally
				float blockageAccumulatedSoFar = 0.0f;
				int distanceToLastTile = 0;
				float accumulatedSinceLastTile = 0;
				int numberTilesWithBlockage = 0;
				auto ourTile = (Vec3<int>)position;
				for (auto t : c.tilesPassed)
				{
					auto vec = t->position;
					if (vec == ourTile)
					{
						continue;
					}

					// Apply vision blockage if we passed at least 1 tile
					auto thisDistance = sqrtf((vec.x - position.x) * (vec.x - position.x) +
					                          (vec.y - position.y) * (vec.y - position.y) +
					                          (vec.z - position.z) * (vec.z - position.z));
					if ((int)thisDistance > distanceToLastTile)
					{
						if (numberTilesWithBlockage > 0)
						{
							blockageAccumulatedSoFar += accumulatedSinceLastTile *
							                            ((int)thisDistance - distanceToLastTile) /
							                            numberTilesWithBlockage;
						}
						distanceToLastTile = thisDistance;
						accumulatedSinceLastTile = 0;
						numberTilesWithBlockage = 0;
					}

					// Reached end of LOS with accumulated blockage
					if ((int)(thisDistance + blockageAccumulatedSoFar) > LOS_RANGE)
					{
						break;
					}

					// Add this tile's vision blockage to accumulated since last tile blockage
					auto thisBlockage = t->visionBlockage;
					if (thisBlockage > 0)
					{
						accumulatedSinceLastTile += thisBlockage;
						numberTilesWithBlockage++;
					}

					// FIXME: This check should be removed after I figure out issues with seeing up
					// through floors
					if (!battle.getVisible(owner, vec.x, vec.y, vec.z))
					{
						battle.setVisible(owner, vec.x, vec.y, vec.z);
					}
					auto unitOccupying = t->getUnitIfPresent(true, true);
					if (unitOccupying)
					{
						auto u = unitOccupying->getUnit();
						if (u->owner != owner)
						{
							visibleUnits.insert({&state, u->id});
						}
					}
				}
			}
		}
	}

	// Add newly visible units to owner's list
	for (auto vu : visibleUnits)
	{
		if (lastVisibleUnits.find(vu) == lastVisibleUnits.end())
		{
			state.current_battle->visibleUnits[owner].insert(vu);
		}
	}

	// See if someone else sees a unit we stopped seeing
	for (auto lvu : lastVisibleUnits)
	{
		if (visibleUnits.find(lvu) == visibleUnits.end())
		{
			bool someoneElseSees = false;
			for (auto u : state.current_battle->units)
			{
				if (u.second->owner != owner)
				{
					continue;
				}
				if (u.second->visibleUnits.find(lvu) != u.second->visibleUnits.end())
				{
					someoneElseSees = true;
					break;
				}
			}
			if (!someoneElseSees)
			{
				state.current_battle->visibleUnits[owner].erase(lvu);
			}
		}
	}
}

void BattleUnit::updateUnitVisibilityAndVision(GameState &state)
{
	updateUnitVision(state);
	updateUnitVisibility(state);
}

void BattleUnit::resetGoal()
{
	goalPosition = position;
	goalFacing = facing;
	atGoal = true;
}

void BattleUnit::setFocus(GameState &state, StateRef<BattleUnit> unit)
{
	StateRef<BattleUnit> sru = {&state, id};
	if (focusUnit)
	{
		auto it =
		    std::find(focusUnit->focusedByUnits.begin(), focusUnit->focusedByUnits.end(), sru);
		if (it != focusUnit->focusedByUnits.end())
		{
			focusUnit->focusedByUnits.erase(it);
		}
		else
		{
			LogError("Inconsistent focusUnit/focusBy!");
		}
	}
	focusUnit = unit;
	focusUnit->focusedByUnits.push_back(sru);
}

void BattleUnit::startAttacking(GameState &state, WeaponStatus status)
{
	switch (state.current_battle->mode)
	{
		case Battle::Mode::TurnBased:
			// In Turn based we cannot override firing
			if (isAttacking())
			{
				return;
			}
			// In Turn based we cannot fire both hands
			if (status == WeaponStatus::FiringBothHands)
			{
				// Right hand has priority
				auto rhItem = agent->getFirstItemInSlot(AEquipmentSlotType::RightHand);
				if (rhItem && rhItem->canFire())
				{
					status = WeaponStatus::FiringRightHand;
				}
				else
				{
					// We don't care what's in the left hand,
					// we will just cancel firing in update() if there's nothing to fire
					status = WeaponStatus::FiringLeftHand;
				}
			}
			break;
		case Battle::Mode::RealTime:
			// Start firing both hands if added one hand to another
			if ((weaponStatus == WeaponStatus::FiringLeftHand &&
			     status == WeaponStatus::FiringRightHand) ||
			    (weaponStatus == WeaponStatus::FiringRightHand &&
			     status == WeaponStatus::FiringLeftHand))
			{
				status = WeaponStatus::FiringBothHands;
			}
			break;
	}

	weaponStatus = status;
	ticksTillNextTargetCheck = 0;
}

void BattleUnit::startAttacking(GameState &state, StateRef<BattleUnit> unit, WeaponStatus status)
{
	startAttacking(state, status);
	targetUnit = unit;
	targetingMode = TargetingMode::Unit;
}

void BattleUnit::startAttacking(GameState &state, Vec3<int> tile, WeaponStatus status,
                                bool atGround)
{
	startAttacking(state, status);
	targetTile = tile;
	targetTile = tile;
	targetingMode = atGround ? TargetingMode::TileGround : TargetingMode::TileCenter;
}

void BattleUnit::stopAttacking()
{
	weaponStatus = WeaponStatus::NotFiring;
	targetingMode = TargetingMode::NoTarget;
	targetUnit.clear();
	ticksTillNextTargetCheck = 0;
}
bool BattleUnit::canAfford(GameState &state, int cost) const
{
	if (state.current_battle->mode == Battle::Mode::RealTime)
	{
		return true;
	}
	return agent->modified_stats.time_units >= cost;
}

bool BattleUnit::spendTU(GameState &state, int cost)
{
	if (state.current_battle->mode == Battle::Mode::RealTime)
	{
		return true;
	}
	if (cost > agent->modified_stats.time_units)
	{
		return false;
	}
	agent->modified_stats.time_units -= cost;
	return true;
}

int BattleUnit::getMaxHealth() const { return this->agent->current_stats.health; }

int BattleUnit::getHealth() const { return this->agent->modified_stats.health; }

int BattleUnit::getMaxShield() const
{
	int maxShield = 0;

	for (auto &e : this->agent->equipment)
	{
		if (e->type->type != AEquipmentType::Type::DisruptorShield)
			continue;
		maxShield += e->type->max_ammo;
	}

	return maxShield;
}

int BattleUnit::getShield() const
{
	int curShield = 0;

	for (auto &e : this->agent->equipment)
	{
		if (e->type->type != AEquipmentType::Type::DisruptorShield)
			continue;
		curShield += e->ammo;
	}

	return curShield;
}

int BattleUnit::getStunDamage() const
{
	// FIXME: Figure out stun damage scale
	int SCALE = TICKS_PER_SECOND;
	return stunDamageInTicks / SCALE;
}

bool BattleUnit::isDead() const { return getHealth() <= 0 || destroyed; }

bool BattleUnit::isUnconscious() const { return !isDead() && getStunDamage() >= getHealth(); }

bool BattleUnit::isConscious() const
{
	return !isDead() && getStunDamage() < getHealth() &&
	       (current_body_state != BodyState::Downed || target_body_state != BodyState::Downed);
}

bool BattleUnit::isStatic() const
{
	if (falling)
	{
		return false;
	}
	if (!missions.empty() && missions.front()->type == BattleUnitMission::Type::AcquireTU)
	{
		return true;
	}
	for (auto &m : missions)
	{
		switch (m->type)
		{
			case BattleUnitMission::Type::ChangeBodyState:
			case BattleUnitMission::Type::ReachGoal:
			case BattleUnitMission::Type::ThrowItem:
			case BattleUnitMission::Type::Turn:
			case BattleUnitMission::Type::GotoLocation:
				return false;
			case BattleUnitMission::Type::AcquireTU:
			case BattleUnitMission::Type::DropItem:
			case BattleUnitMission::Type::RestartNextMission:
			case BattleUnitMission::Type::Snooze:
			case BattleUnitMission::Type::Teleport:
				break;
		}
	}
	return true;
}

bool BattleUnit::isBusy() const { return !isStatic() || isAttacking(); }

bool BattleUnit::isAttacking() const { return weaponStatus != WeaponStatus::NotFiring; }
bool BattleUnit::isThrowing() const
{
	bool throwing = false;
	for (auto &m : missions)
	{
		if (m->type == BattleUnitMission::Type::ThrowItem)
		{
			throwing = true;
			break;
		}
	}
	return throwing;
}

bool BattleUnit::canFly() const
{
	return isConscious() && agent->isBodyStateAllowed(BodyState::Flying);
}

bool BattleUnit::canMove() const
{
	if (!isConscious())
	{
		return false;
	}
	if (agent->isMovementStateAllowed(MovementState::Normal) ||
	    agent->isMovementStateAllowed(MovementState::Running))
	{
		return true;
	}
	return false;
}

bool BattleUnit::canProne(Vec3<int> pos, Vec2<int> fac) const
{
	if (isLarge())
	{
		LogError("Large unit attempting to go prone? WTF? Should large units ever acces this?");
		return false;
	}
	// Check if agent can go prone and stand in its current tile
	if (!agent->isBodyStateAllowed(BodyState::Prone) || !tileObject->getOwningTile()->getCanStand())
		return false;
	// Check if agent can put legs in the tile behind. Conditions
	// 1) Target tile provides standing ability
	// 2) Target tile height is not too big compared to current tile
	// 3) Target tile is passable
	// 4) Target tile has no unit occupying it (other than us)
	Vec3<int> legsPos = pos - Vec3<int>{fac.x, fac.y, 0};
	if ((legsPos.x >= 0) && (legsPos.x < tileObject->map.size.x) && (legsPos.y >= 0) &&
	    (legsPos.y < tileObject->map.size.y) && (legsPos.z >= 0) &&
	    (legsPos.z < tileObject->map.size.z))
	{
		auto bodyTile = tileObject->map.getTile(pos);
		auto legsTile = tileObject->map.getTile(legsPos);
		if (legsTile->canStand && bodyTile->canStand &&
		    std::abs(legsTile->height - bodyTile->height) <= 0.25f &&
		    legsTile->getPassable(false, agent->type->bodyType->height.at(BodyState::Prone)) &&
		    (legsPos == (Vec3<int>)position || !legsTile->getUnitIfPresent(true, true)))
		{
			return true;
		}
	}
	return false;
}

bool BattleUnit::canKneel() const
{
	if (!agent->isBodyStateAllowed(BodyState::Kneeling) ||
	    !tileObject->getOwningTile()->getCanStand(isLarge()))
		return false;
	return true;
}

void BattleUnit::addFatalWound(GameState &state, BodyPart fatalWoundPart)
{
	fatalWounds[fatalWoundPart]++;
}

void BattleUnit::dealDamage(GameState &state, int damage, bool generateFatalWounds,
                            BodyPart fatalWoundPart, int stunPower)
{
	bool wasConscious = isConscious();
	bool fatal = false;

	// Deal stun damage
	if (stunPower > 0)
	{
		// FIXME: Figure out stun damage scale
		int SCALE = TICKS_PER_SECOND;

		stunDamageInTicks +=
		    clamp(damage * SCALE, 0, std::max(0, stunPower * SCALE - stunDamageInTicks));
	}
	// Deal health damage
	else
	{
		agent->modified_stats.health -= damage;
	}

	// Generate fatal wounds
	if (generateFatalWounds)
	{
		int woundDamageRemaining = damage;
		while (woundDamageRemaining > 10)
		{
			woundDamageRemaining -= 10;
			addFatalWound(state, fatalWoundPart);
			fatal = true;
		}
		if (randBoundsExclusive(state.rng, 0, 10) < woundDamageRemaining)
		{
			addFatalWound(state, fatalWoundPart);
			fatal = true;
		}
	}

	// Die or go unconscious
	if (isDead())
	{
		LogWarning("Handle violent deaths");
		die(state, true);
		return;
	}
	else if (!isConscious() && wasConscious)
	{
		fallUnconscious(state);
	}

	// Emit sound fatal wound
	if (fatal)
	{
		if (agent->type->fatalWoundSfx.find(agent->gender) != agent->type->fatalWoundSfx.end() &&
		    !agent->type->fatalWoundSfx.at(agent->gender).empty())
		{
			fw().soundBackend->playSample(
			    listRandomiser(state.rng, agent->type->fatalWoundSfx.at(agent->gender)), position);
		}
	}
	// Emit sound wound (unless if dealing damage from a fatal wound)
	else if (stunPower == 0 && generateFatalWounds)
	{
		if (agent->type->damageSfx.find(agent->gender) != agent->type->damageSfx.end() &&
		    !agent->type->damageSfx.at(agent->gender).empty())
		{
			fw().soundBackend->playSample(
			    listRandomiser(state.rng, agent->type->damageSfx.at(agent->gender)), position);
		}
	}

	return;
}

bool BattleUnit::applyDamage(GameState &state, int power, StateRef<DamageType> damageType,
                             BodyPart bodyPart)
{
	if (damageType->doesImpactDamage())
	{
		fw().soundBackend->playSample(listRandomiser(state.rng, *genericHitSounds), position);
	}

	// Calculate damage
	int damage;
	bool USER_OPTION_UFO_DAMAGE_MODEL = false;
	if (damageType->effectType == DamageType::EffectType::Smoke) // smoke deals 1-3 stun damage
	{
		power = 2;
		damage = randDamage050150(state.rng, power);
	}
	else if (damageType->explosive) // explosive deals 50-150% damage
	{
		damage = randDamage050150(state.rng, power);
	}
	else if (USER_OPTION_UFO_DAMAGE_MODEL)
	{
		damage = randDamage000200(state.rng, power);
	}
	else
	{
		damage = randDamage050150(state.rng, power);
	}

	// Hit shield if present
	if (!damageType->ignore_shield)
	{
		auto shield = agent->getFirstShield();
		if (shield)
		{
			damage = damageType->dealDamage(damage, shield->type->damage_modifier);
			shield->ammo -= damage;
			// Shield destroyed
			if (shield->ammo <= 0)
			{
				agent->removeEquipment(shield);
			}
			state.current_battle->placeDoodad({&state, "DOODAD_27_SHIELD"},
			                                  tileObject->getCenter());
			return true;
		}
	}

	// Calculate damage to armor type
	auto armor = agent->getArmor(bodyPart);
	int armorValue = 0;
	StateRef<DamageModifier> damageModifier;
	if (armor)
	{
		armorValue = armor->ammo;
		damageModifier = armor->type->damage_modifier;
	}
	else
	{
		armorValue = agent->type->armor.at(bodyPart);
		damageModifier = agent->type->damage_modifier;
	}
	// Smoke ignores armor value but does not ignore damage modifier
	damage = damageType->dealDamage(damage, damageModifier) -
	         (damageType->ignoresArmorValue() ? 0 : armorValue);

	// No daamge
	if (damage <= 0)
	{
		return false;
	}

	// Smoke, fire and stun damage does not damage armor
	if (damageType->dealsArmorDamage() && armor)
	{
		// Armor damage
		int armorDamage = damage / 10 + 1;
		armor->ammo -= armorDamage;
		// Armor destroyed
		if (armor->ammo <= 0)
		{
			agent->removeEquipment(armor);
		}
	}

	// Apply damage according to type
	dealDamage(state, damage, damageType->dealsFatalWounds(), bodyPart,
	           damageType->dealsStunDamage() ? power : 0);

	return false;
}

BodyPart BattleUnit::determineBodyPartHit(StateRef<DamageType> damageType, Vec3<float> cposition,
                                          Vec3<float> direction)
{
	BodyPart bodyPartHit = BodyPart::Body;

	// FIXME: Ensure body part determination is correct
	// Assume top 25% is head, lower 25% is legs, and middle 50% is body/left/right
	float altitude = (cposition.z - position.z) * 40.0f / (float)getCurrentHeight();
	if (damageType->alwaysImpactsHead()) // gas deals damage to the head
	{
		bodyPartHit = BodyPart::Helmet;
	}
	else if (altitude > 0.75f)
	{
		bodyPartHit = BodyPart::Helmet;
	}
	else if (altitude < 0.25f)
	{
		bodyPartHit = BodyPart::Legs;
	}
	else
	{
		auto unitDir = glm::normalize(Vec3<float>{facing.x, facing.y, 0.0f});
		auto projectileDir = glm::normalize(Vec3<float>{direction.x, direction.y, 0.0f});
		auto cross = glm::cross(unitDir, projectileDir);
		int angle =
		    (int)((cross.z >= 0 ? -1 : 1) * glm::angle(unitDir, -projectileDir) / M_PI * 180.0f);
		if (angle > 45 && angle < 135)
		{
			bodyPartHit = BodyPart::RightArm;
		}
		else if (angle < -45 && angle > -135)
		{
			bodyPartHit = BodyPart::LeftArm;
		}
	}
	return bodyPartHit;
}

bool BattleUnit::handleCollision(GameState &state, Collision &c)
{
	std::ignore = state;

	// Corpses do not handle collision
	if (isDead())
		return false;

	if (!this->tileObject)
	{
		LogError("It's possible multiple projectiles hit the same tile in the same tick (?)");
		return false;
	}

	auto projectile = c.projectile.get();
	if (projectile)
	{
		return applyDamage(
		    state, projectile->damage, projectile->damageType,
		    determineBodyPartHit(projectile->damageType, c.position, projectile->getVelocity()));
	}
	return false;
}

void BattleUnit::update(GameState &state, unsigned int ticks)
{
	// Destroyed or retreated units do not exist in the battlescape
	if (destroyed || retreated)
	{
		return;
	}

	// Init
	//
	auto &map = tileObject->map;

	// Update other classes
	//
	for (auto item : agent->equipment)
		item->update(state, ticks);

	if (!this->missions.empty())
		this->missions.front()->update(state, *this, ticks);

	// Update our stats and state
	//

	// FIXME: Regenerate stamina

	// Stun removal
	if (stunDamageInTicks > 0)
	{
		stunDamageInTicks = std::max(0, stunDamageInTicks - (int)ticks);
	}

	// Ensure still have item if healing
	if (isHealing)
	{
		isHealing = false;
		auto e1 = agent->getFirstItemInSlot(AEquipmentSlotType::LeftHand);
		auto e2 = agent->getFirstItemInSlot(AEquipmentSlotType::RightHand);
		if (e1 && e1->type->type == AEquipmentType::Type::MediKit)
		{
			isHealing = true;
		}
		else if (e2 && e2->type->type == AEquipmentType::Type::MediKit)
		{
			isHealing = true;
		}
	}

	// Fatal wounds / healing
	if (isFatallyWounded() && !isDead())
	{
		bool unconscious = isUnconscious();
		woundTicksAccumulated += ticks;
		while (woundTicksAccumulated > TICKS_PER_UNIT_EFFECT)
		{
			woundTicksAccumulated -= TICKS_PER_UNIT_EFFECT;
			for (auto &w : fatalWounds)
			{
				if (w.second > 0)
				{
					dealDamage(state, w.second, false, BodyPart::Body, 0);
					if (isHealing && healingBodyPart == w.first)
					{
						w.second--;
						// healing fatal wound heals 3hp, as well as 1hp we just dealt in damage
						agent->modified_stats.health += 4;
						agent->modified_stats.health =
						    std::min(agent->modified_stats.health, agent->current_stats.health);
					}
				}
			}
		}
		// If fully healed the body part
		if (isHealing && fatalWounds[healingBodyPart] == 0)
		{
			isHealing = false;
		}
		// If died or went unconscious
		if (isDead())
		{
			die(state, true, true);
		}
		if (!unconscious && isUnconscious())
		{
			fallUnconscious(state);
		}
	} // End of Fatal Wounds and Healing

	// Idling check
	if (missions.empty() && isConscious())
	{
		// Sanity checks
		if (goalFacing != facing)
		{
			LogError("Unit turning without a mission, wtf?");
		}
		if (target_body_state != current_body_state)
		{
			LogError("Unit changing body state without a mission, wtf?");
		}

		// Reach goal before everything else
		if (!atGoal)
		{
			addMission(state, BattleUnitMission::Type::ReachGoal);
		}

		// Try giving way if asked to
		// FIXME: Ensure we're not in a firefight before giving way!
		else if (giveWayRequestData.size() > 0)
		{
			// If we're given a giveWay request 0, 0 it means we're asked to kneel temporarily
			if (giveWayRequestData.size() == 1 && giveWayRequestData.front().x == 0 &&
			    giveWayRequestData.front().y == 0 &&
			    canAfford(state, BattleUnitMission::getBodyStateChangeCost(*this, target_body_state,
			                                                               BodyState::Kneeling)))
			{
				// Give way
				setMission(state, BattleUnitMission::changeStance(*this, BodyState::Kneeling));
				// Give time for that unit to pass
				addMission(state, BattleUnitMission::snooze(*this, TICKS_PER_SECOND), true);
			}
			else
			{
				auto from = tileObject->getOwningTile();
				for (auto newHeading : giveWayRequestData)
				{
					for (int z = -1; z <= 1; z++)
					{
						if (z < 0 || z >= map.size.z)
						{
							continue;
						}
						// Try the new heading
						Vec3<int> pos = {position.x + newHeading.x, position.y + newHeading.y,
						                 position.z + z};
						auto to = map.getTile(pos);
						// Check if heading on our level is acceptable
						bool acceptable = BattleUnitTileHelper{map, *this}.canEnterTile(from, to) &&
						                  BattleUnitTileHelper{map, *this}.canEnterTile(to, from);
						// If not, check if we can go down one tile
						if (!acceptable && pos.z - 1 >= 0)
						{
							pos -= Vec3<int>{0, 0, 1};
							to = map.getTile(pos);
							acceptable = BattleUnitTileHelper{map, *this}.canEnterTile(from, to) &&
							             BattleUnitTileHelper{map, *this}.canEnterTile(to, from);
						}
						// If not, check if we can go up one tile
						if (!acceptable && pos.z + 2 < map.size.z)
						{
							pos += Vec3<int>{0, 0, 2};
							to = map.getTile(pos);
							acceptable = BattleUnitTileHelper{map, *this}.canEnterTile(from, to) &&
							             BattleUnitTileHelper{map, *this}.canEnterTile(to, from);
						}
						if (acceptable)
						{
							// 01: Give way (move 1 tile away)
							setMission(state, BattleUnitMission::gotoLocation(*this, pos, 0));
							// 02: Turn to previous facing
							addMission(state, BattleUnitMission::turn(*this, facing), true);
							// 03: Give time for that unit to pass
							addMission(state, BattleUnitMission::snooze(*this, 60), true);
							// 04: Return to our position after we're done
							addMission(state, BattleUnitMission::gotoLocation(*this, position, 0),
							           true);
							// 05: Turn to previous facing
							addMission(state, BattleUnitMission::turn(*this, facing), true);
						}
						if (!missions.empty())
						{
							break;
						}
					}
					if (!missions.empty())
					{
						break;
					}
				}
			}
			giveWayRequestData.clear();
		}
		else // if not giving way
		{
			setMovementState(MovementState::None);
			// Kneel if not kneeling and should kneel
			if (kneeling_mode == KneelingMode::Kneeling &&
			    current_body_state != BodyState::Kneeling && canKneel() &&
			    canAfford(state, BattleUnitMission::getBodyStateChangeCost(*this, target_body_state,
			                                                               BodyState::Kneeling)))
			{
				setMission(state, BattleUnitMission::changeStance(*this, BodyState::Kneeling));
			}
			// Go prone if not prone and should stay prone
			else if (movement_mode == MovementMode::Prone &&
			         current_body_state != BodyState::Prone &&
			         kneeling_mode != KneelingMode::Kneeling && canProne(position, facing) &&
			         canAfford(state, BattleUnitMission::getBodyStateChangeCost(
			                              *this, target_body_state, BodyState::Prone)))
			{
				setMission(state, BattleUnitMission::changeStance(*this, BodyState::Prone));
			}
			// Stand up if not standing up and should stand up
			else if ((movement_mode == MovementMode::Walking ||
			          movement_mode == MovementMode::Running) &&
			         kneeling_mode != KneelingMode::Kneeling &&
			         current_body_state != BodyState::Standing &&
			         current_body_state != BodyState::Flying)
			{
				if (agent->isBodyStateAllowed(BodyState::Standing))
				{
					if (canAfford(state, BattleUnitMission::getBodyStateChangeCost(
					                         *this, target_body_state, BodyState::Standing)))
					{
						setMission(state,
						           BattleUnitMission::changeStance(*this, BodyState::Standing));
					}
				}
				else
				{
					if (canAfford(state, BattleUnitMission::getBodyStateChangeCost(
					                         *this, target_body_state, BodyState::Flying)))
					{
						setMission(state,
						           BattleUnitMission::changeStance(*this, BodyState::Flying));
					}
				}
			}
			// Stop flying if we can stand
			else if (current_body_state == BodyState::Flying &&
			         tileObject->getOwningTile()->getCanStand(isLarge()) &&
			         agent->isBodyStateAllowed(BodyState::Standing) &&
			         canAfford(state, BattleUnitMission::getBodyStateChangeCost(
			                              *this, target_body_state, BodyState::Standing)))
			{
				setMission(state, BattleUnitMission::changeStance(*this, BodyState::Standing));
			}
			// Stop being prone if legs are no longer supported and we haven't taken a mission yet
			if (current_body_state == BodyState::Prone && missions.empty())
			{
				bool hasSupport = true;
				for (auto t : tileObject->occupiedTiles)
				{
					if (!map.getTile(t)->getCanStand())
					{
						hasSupport = false;
						break;
					}
				}
				if (!hasSupport &&
				    canAfford(state, BattleUnitMission::getBodyStateChangeCost(
				                         *this, target_body_state, BodyState::Kneeling)))
				{
					setMission(state, BattleUnitMission::changeStance(*this, BodyState::Kneeling));
				}
			}
		}
	} // End of Idling

	// Movement and Body Animation
	{
		bool wasUsingLift = usingLift;
		usingLift = false;

		// Turn off Jetpacks
		if (current_body_state != BodyState::Flying)
		{
			flyingSpeedModifier = 0;
		}

		// If not running we will consume these twice as fast
		unsigned int moveTicksRemaining = ticks * agent->modified_stats.getActualSpeedValue() * 2;
		unsigned int bodyTicksRemaining = ticks;
		unsigned int handTicksRemaining = ticks;
		unsigned int turnTicksRemaining = ticks;

		// Unconscious units cannot move their hands or turn, they can only animate body or fall
		if (!isConscious())
		{
			handTicksRemaining = 0;
			turnTicksRemaining = 0;
		}

		unsigned int lastMoveTicksRemaining = 0;
		unsigned int lastBodyTicksRemaining = 0;
		unsigned int lastHandTicksRemaining = 0;
		unsigned int lastTurnTicksRemaining = 0;

		while (lastMoveTicksRemaining != moveTicksRemaining ||
		       lastBodyTicksRemaining != bodyTicksRemaining ||
		       lastHandTicksRemaining != handTicksRemaining ||
		       lastTurnTicksRemaining != turnTicksRemaining)
		{
			lastMoveTicksRemaining = moveTicksRemaining;
			lastBodyTicksRemaining = bodyTicksRemaining;
			lastHandTicksRemaining = handTicksRemaining;
			lastTurnTicksRemaining = turnTicksRemaining;

			// Begin falling or changing stance to flying if appropriate
			if (!falling)
			{
				// Check if should fall or start flying
				if (!canFly() || current_body_state != BodyState::Flying)
				{
					bool hasSupport = false;
					bool fullySupported = true;
					if (tileObject->getOwningTile()->getCanStand(isLarge()))
					{
						hasSupport = true;
					}
					else
					{
						fullySupported = false;
					}
					if (!atGoal)
					{
						if (map.getTile(goalPosition)->getCanStand(isLarge()))
						{
							hasSupport = true;
						}
						else
						{
							fullySupported = false;
						}
					}
					// If not flying and has no support - fall!
					if (!hasSupport && !canFly())
					{
						startFalling();
					}
					// If flying and not supported both on current and goal locations - start flying
					if (!fullySupported && canFly())
					{
						if (current_body_state == target_body_state)
						{
							setBodyState(state, BodyState::Flying);
							if (!missions.empty())
							{
								missions.front()->targetBodyState = current_body_state;
							}
						}
					}
				}
			}

			// Change body state
			if (bodyTicksRemaining > 0)
			{
				if (body_animation_ticks_remaining > bodyTicksRemaining)
				{
					body_animation_ticks_remaining -= bodyTicksRemaining;
					bodyTicksRemaining = 0;
				}
				else
				{
					if (body_animation_ticks_remaining > 0)
					{
						bodyTicksRemaining -= body_animation_ticks_remaining;
						setBodyState(state, target_body_state);
					}
					// Pop finished missions if present
					if (popFinishedMissions(state))
					{
						return;
					}
					// Try to get new body state change
					// Can do it if we're not firing and (either not changing hand state, or
					// starting to aim)
					if (firing_animation_ticks_remaining == 0 &&
					    (hand_animation_ticks_remaining == 0 ||
					     target_hand_state == HandState::Aiming))
					{
						BodyState nextState = BodyState::Downed;
						if (getNextBodyState(state, nextState))
						{
							beginBodyStateChange(state, nextState);
						}
					}
				}
			}

			// Change hand state
			if (handTicksRemaining > 0)
			{
				if (firing_animation_ticks_remaining > 0)
				{
					if (firing_animation_ticks_remaining > handTicksRemaining)
					{
						firing_animation_ticks_remaining -= handTicksRemaining;
						handTicksRemaining = 0;
					}
					else
					{
						handTicksRemaining -= firing_animation_ticks_remaining;
						firing_animation_ticks_remaining = 0;
						setHandState(HandState::Aiming);
					}
				}
				else
				{
					if (hand_animation_ticks_remaining > handTicksRemaining)
					{
						hand_animation_ticks_remaining -= handTicksRemaining;
						handTicksRemaining = 0;
					}
					else
					{
						if (hand_animation_ticks_remaining > 0)
						{
							handTicksRemaining -= hand_animation_ticks_remaining;
							hand_animation_ticks_remaining = 0;
							setHandState(target_hand_state);
						}
					}
				}
			}

			// Try moving
			if (moveTicksRemaining > 0)
			{
				// If falling then process falling
				if (falling)
				{
					// Falling consumes remaining move ticks
					auto fallTicksRemaining =
					    moveTicksRemaining / (agent->modified_stats.getActualSpeedValue() * 2);
					moveTicksRemaining = 0;

					// Process falling
					auto newPosition = position;
					while (fallTicksRemaining-- > 0)
					{
						fallingSpeed += FALLING_ACCELERATION_UNIT;
						newPosition -= Vec3<float>{0.0f, 0.0f, (fallingSpeed / TICK_SCALE)} /
						               VELOCITY_SCALE_BATTLE;
					}
					// Fell into a unit
					if (isConscious() &&
					    map.getTile(newPosition)->getUnitIfPresent(true, true, false, tileObject))
					{
						// FIXME: Proper stun damage (ensure it is!)
						stunDamageInTicks = 0;
						dealDamage(state, agent->current_stats.health * 3 / 2, false,
						           BodyPart::Body, 9001);
						fallUnconscious(state);
					}
					setPosition(state, newPosition);
					triggerProximity(state);

					// Falling units can always turn
					goalPosition = position;
					atGoal = true;

					// Check if reached ground
					auto restingPosition =
					    tileObject->getOwningTile()->getRestingPosition(isLarge());
					if (position.z < restingPosition.z)
					{
						// Stopped falling
						falling = false;
						if (!isConscious())
						{
							// Bodies drop to the exact spot they fell upon
							setPosition(state, {position.x, position.y, restingPosition.z});
						}
						else
						{
							setPosition(state, restingPosition);
						}
						triggerProximity(state);
						resetGoal();
						// FIXME: Deal fall damage before nullifying this
						// FIXME: Play falling sound
						fallingSpeed = 0;
					}
				}

				// We are moving and not falling
				else if (current_movement_state != MovementState::None)
				{
					unsigned int speedModifier = 100;
					if (current_body_state == BodyState::Flying)
					{
						speedModifier = std::max((unsigned)1, flyingSpeedModifier);
					}

					Vec3<float> vectorToGoal = goalPosition - getPosition();
					unsigned int distanceToGoal = (unsigned)ceilf(glm::length(
					    vectorToGoal * VELOCITY_SCALE_BATTLE * (float)TICKS_PER_UNIT_TRAVELLED));
					unsigned int moveTicksConsumeRate =
					    current_movement_state == MovementState::Running ? 1 : 2;

					// Quick check, if moving strictly vertical then using lift
					if (distanceToGoal > 0 && current_body_state != BodyState::Flying &&
					    vectorToGoal.x == 0 && vectorToGoal.y == 0)
					{
						// FIXME: Actually read set option
						bool USER_OPTION_GRAVLIFT_SOUNDS = true;
						if (USER_OPTION_GRAVLIFT_SOUNDS && !wasUsingLift)
						{
							fw().soundBackend->playSample(agent->type->gravLiftSfx, getPosition(),
							                              0.25f);
						}
						usingLift = true;
						movement_ticks_passed = 0;
					}
					unsigned int movementTicksAccumulated = 0;
					if (distanceToGoal * moveTicksConsumeRate * 100 / speedModifier >
					    moveTicksRemaining)
					{
						if (flyingSpeedModifier != 100)
						{
							flyingSpeedModifier = std::min(
							    (unsigned)100, flyingSpeedModifier +
							                       moveTicksRemaining / moveTicksConsumeRate /
							                           FLYING_ACCELERATION_DIVISOR);
						}
						movementTicksAccumulated = moveTicksRemaining / moveTicksConsumeRate;
						auto dir = glm::normalize(vectorToGoal);
						Vec3<float> newPosition =
						    (float)(moveTicksRemaining / moveTicksConsumeRate) *
						    (float)(speedModifier / 100) * dir;
						newPosition /= VELOCITY_SCALE_BATTLE;
						newPosition /= (float)TICKS_PER_UNIT_TRAVELLED;
						newPosition += getPosition();
						setPosition(state, newPosition);
						triggerProximity(state);
						moveTicksRemaining = moveTicksRemaining % moveTicksConsumeRate;
						atGoal = false;
					}
					else
					{
						if (distanceToGoal > 0)
						{
							movementTicksAccumulated = distanceToGoal;
							if (flyingSpeedModifier != 100)
							{
								flyingSpeedModifier =
								    std::min((unsigned)100,
								             flyingSpeedModifier +
								                 distanceToGoal / FLYING_ACCELERATION_DIVISOR);
							}
							moveTicksRemaining -= distanceToGoal * moveTicksConsumeRate;
							setPosition(state, goalPosition);
							triggerProximity(state);
							goalPosition = getPosition();
						}
						atGoal = true;
						// Pop finished missions if present
						if (popFinishedMissions(state))
						{
							return;
						}
						// Try to get new destination
						Vec3<float> nextGoal;
						if (getNextDestination(state, nextGoal))
						{
							goalPosition = nextGoal;
							atGoal = false;
						}
					}

					// Scale ticks so that animations look proper on isometric sceen
					// facing down or up on screen
					if (facing.x == facing.y)
					{
						movement_ticks_passed += movementTicksAccumulated * 100 / 150;
					}
					// facing left or right on screen
					else if (facing.x == -facing.y)
					{
						movement_ticks_passed += movementTicksAccumulated * 141 / 150;
					}
					else
					{
						movement_ticks_passed += movementTicksAccumulated;
					}
					// Footsteps sound
					if (shouldPlaySoundNow() && current_body_state != BodyState::Flying)
					{
						if (agent->type->walkSfx.size() > 0)
						{
							fw().soundBackend->playSample(
							    agent->type
							        ->walkSfx[getWalkSoundIndex() % agent->type->walkSfx.size()],
							    getPosition(), 0.25f);
						}
						else
						{
							auto t = tileObject->getOwningTile();
							if (t->walkSfx && t->walkSfx->size() > 0)
							{
								fw().soundBackend->playSample(
								    t->walkSfx->at(getWalkSoundIndex() % t->walkSfx->size()),
								    getPosition(), 0.25f);
							}
						}
					}
				}
				// We are not moving and not falling
				else
				{
					// Check if we should adjust our current position
					if (goalPosition == getPosition())
					{
						goalPosition = tileObject->getOwningTile()->getRestingPosition(isLarge());
					}
					atGoal = goalPosition == getPosition();
					// If not at goal - go to goal
					if (!atGoal)
					{
						addMission(state, BattleUnitMission::Type::ReachGoal);
					}
					// If at goal - try to request new destination
					else
					{
						// Pop finished missions if present
						if (popFinishedMissions(state))
						{
							return;
						}
						// Try to get new destination
						Vec3<float> nextGoal;
						if (getNextDestination(state, nextGoal))
						{
							goalPosition = nextGoal;
							atGoal = false;
						}
					}
				}
			}

			// Try turning
			if (turnTicksRemaining > 0)
			{
				if (turning_animation_ticks_remaining > turnTicksRemaining)
				{
					turning_animation_ticks_remaining -= turnTicksRemaining;
					turnTicksRemaining = 0;
				}
				else
				{
					if (turning_animation_ticks_remaining > 0)
					{
						turnTicksRemaining -= turning_animation_ticks_remaining;
						setFacing(state, goalFacing);
					}
					// Pop finished missions if present
					if (popFinishedMissions(state))
					{
						return;
					}
					// Try to get new facing change
					Vec2<int> nextFacing;
					if (getNextFacing(state, nextFacing))
					{
						beginTurning(state, nextFacing);
					}
				}
			}

			updateDisplayedItem();
		}

	} // End of Movement and Body Animation

	// Firing

	static const Vec3<float> offsetTile = {0.5f, 0.5f, 0.0f};
	static const Vec3<float> offsetTileGround = {0.5f, 0.5f, 10.0f / 40.0f};
	Vec3<float> muzzleLocation = getMuzzleLocation();
	Vec3<float> targetPosition;
	switch (targetingMode)
	{
		case TargetingMode::Unit:
			targetPosition = targetUnit->tileObject->getVoxelCentrePosition();
			break;
		case TargetingMode::TileCenter:
		{
			// Shoot parallel to the ground
			float unitZ = muzzleLocation.z;
			unitZ -= (int)unitZ;
			targetPosition = (Vec3<float>)targetTile + offsetTile + Vec3<float>{0.0f, 0.0f, unitZ};
			break;
		}
		case TargetingMode::TileGround:
			targetPosition = (Vec3<float>)targetTile + offsetTileGround;
			break;
		case TargetingMode::NoTarget:
			// Ain't need to do anythin!
			break;
	}

	// For simplicity, prepare weapons we can use
	// We can use a weapon if we're set to fire this hand, and it's a weapon that can be fired

	auto weaponRight = agent->getFirstItemInSlot(AEquipmentSlotType::RightHand);
	auto weaponLeft = agent->getFirstItemInSlot(AEquipmentSlotType::LeftHand);
	switch (weaponStatus)
	{
		case WeaponStatus::FiringBothHands:
			if (weaponRight && weaponRight->needsReload())
			{
				weaponRight->loadAmmo(state);
			}
			if (weaponRight && !weaponRight->canFire())
			{
				weaponRight = nullptr;
			}
			if (weaponLeft && weaponLeft->needsReload())
			{
				weaponLeft->loadAmmo(state);
			}
			if (weaponLeft && !weaponLeft->canFire())
			{
				weaponLeft = nullptr;
			}
			break;
		case WeaponStatus::FiringRightHand:
			if (weaponRight && weaponRight->needsReload())
			{
				weaponRight->loadAmmo(state);
			}
			if (weaponRight && !weaponRight->canFire())
			{
				weaponRight = nullptr;
			}
			weaponLeft = nullptr;
			break;
		case WeaponStatus::FiringLeftHand:
			if (weaponLeft && weaponLeft->needsReload())
			{
				weaponLeft->loadAmmo(state);
			}
			if (weaponLeft && !weaponLeft->canFire())
			{
				weaponLeft = nullptr;
			}
			weaponRight = nullptr;
			break;
		case WeaponStatus::NotFiring:
			// Ain't need to do anythin!
			break;
	}

	// Firing - check if we should stop firing
	if (isAttacking())
	{
		if (targetingMode == TargetingMode::Unit)
		{
			if (ticksTillNextTargetCheck > ticks)
			{
				ticksTillNextTargetCheck -= ticks;
			}
			else
			{
				ticksTillNextTargetCheck = 0;
			}
		}

		// Do consequent checks, if previous is ok
		bool canFire = true;

		// We cannot fire if we have no weapon capable of firing
		canFire = canFire && (weaponLeft || weaponRight);

		// We cannot fire if it's time to check target unit and it's not in LOS anymore or not
		// conscious
		// Also, at this point we will turn to target tile if targeting tile
		if (canFire)
		{
			// Note: If not targeting a unit, this will only be done once after start,
			// and again once each time unit stops moving
			if (ticksTillNextTargetCheck == 0)
			{
				ticksTillNextTargetCheck = LOS_CHECK_INTERVAL_TRACKING;
				if (targetingMode == TargetingMode::Unit)
				{
					canFire = canFire && targetUnit->isConscious();
					// FIXME: IMPLEMENT LOS CHECKING
					canFire = canFire && true; // Here we check if target is visible
					if (canFire)
					{
						targetTile = targetUnit->position;
					}
				}
				// Check if we are in range
				if (canFire)
				{
					if (weaponRight && !weaponRight->canFire(targetPosition))
					{
						weaponRight = nullptr;
					}
					if (weaponLeft && !weaponLeft->canFire(targetPosition))
					{
						weaponLeft = nullptr;
					}
					// We cannot fire if both weapons are out of range
					canFire = canFire && (weaponLeft || weaponRight);
				}
				// Check if we should turn to target tile (only do this if stationary)
				if (canFire && current_movement_state == MovementState::None)
				{
					auto m = BattleUnitMission::turn(*this, targetTile);
					if (!m->isFinished(state, *this, false))
					{
						addMission(state, m);
					}
				}
			}
		}

		// Finally if any of the checks failed - stop firing
		if (!canFire)
		{
			stopAttacking();
		}
	}

	// Firing - process unit that is firing
	if (isAttacking())
	{
		// Should we start firing a gun?
		if (target_hand_state == HandState::Aiming)
		{
			if (weaponRight && !weaponRight->isFiring())
			{
				weaponRight->startFiring(fire_aiming_mode);
			}
			if (weaponLeft && !weaponLeft->isFiring())
			{
				weaponLeft->startFiring(fire_aiming_mode);
			}
		}

		// Is a gun ready to fire?
		bool weaponFired = false;
		if (firing_animation_ticks_remaining == 0 && target_hand_state == HandState::Aiming)
		{
			sp<AEquipment> firingWeapon = nullptr;
			if (weaponRight && weaponRight->readyToFire)
			{
				firingWeapon = weaponRight;
				weaponRight = nullptr;
			}
			else if (weaponLeft && weaponLeft->readyToFire)
			{
				firingWeapon = weaponLeft;
				weaponLeft = nullptr;
			}
			// Check if facing the right way
			if (firingWeapon)
			{
				auto targetVector = targetPosition - muzzleLocation;
				targetVector = {targetVector.x, targetVector.y, 0.0f};
				// Target must be within frontal arc
				if (glm::angle(glm::normalize(targetVector),
				               glm::normalize(Vec3<float>{facing.x, facing.y, 0})) >= M_PI / 2)
				{
					firingWeapon = nullptr;
				}
			}
			// If still OK - fire!
			if (firingWeapon)
			{
				firingWeapon->fire(state, targetPosition,
				                   targetingMode == TargetingMode::Unit ? targetUnit : nullptr);
				displayedItem = firingWeapon->type;
				setHandState(HandState::Firing);
				weaponFired = true;
			}
		}

		// If fired weapon at ground or ally - stop firing that hand
		if (weaponFired && (targetingMode != TargetingMode::Unit || targetUnit->owner == owner))
		{
			switch (weaponStatus)
			{
				case WeaponStatus::FiringBothHands:
					if (!weaponRight)
					{
						if (!weaponLeft)
						{
							stopAttacking();
						}
						else
						{
							weaponStatus = WeaponStatus::FiringLeftHand;
						}
					}
					else if (!weaponLeft)
					{
						weaponStatus = WeaponStatus::FiringRightHand;
					}
					break;
				case WeaponStatus::FiringLeftHand:
					if (!weaponLeft)
					{
						stopAttacking();
					}
					break;
				case WeaponStatus::FiringRightHand:
					if (!weaponRight)
					{
						stopAttacking();
					}
					break;
				case WeaponStatus::NotFiring:
					LogError("Weapon fired while not firing?");
					break;
			}
		}

		// Should we start aiming?
		if (firing_animation_ticks_remaining == 0 && hand_animation_ticks_remaining == 0 &&
		    body_animation_ticks_remaining == 0 && current_hand_state != HandState::Aiming &&
		    current_movement_state != MovementState::Running &&
		    current_movement_state != MovementState::Strafing &&
		    !(current_body_state == BodyState::Prone &&
		      current_movement_state != MovementState::None))
		{
			beginHandStateChange(HandState::Aiming);
		}

	} // end if Firing - process firing

	// Not Firing (or may have just stopped firing)
	if (!isAttacking())
	{
		// Should we stop aiming?
		if (aiming_ticks_remaining > 0)
		{
			aiming_ticks_remaining -= ticks;
		}
		else if (firing_animation_ticks_remaining == 0 && hand_animation_ticks_remaining == 0 &&
		         current_hand_state == HandState::Aiming)
		{
			beginHandStateChange(HandState::AtEase);
		}
	} // end if not Firing

	// FIXME: Soldier "thinking" (auto-attacking, auto-turning)
}

void BattleUnit::triggerProximity(GameState &state)
{
	auto it = state.current_battle->items.begin();
	while (it != state.current_battle->items.end())
	{
		auto i = *it++;
		if (!i->item->primed || i->item->triggerDelay > 0)
		{
			continue;
		}
		// Proximity explosion trigger
		if ((i->item->triggerType == TriggerType::Proximity ||
		     i->item->triggerType == TriggerType::Boomeroid) &&
		    BattleUnitTileHelper::getDistanceStatic(position, i->position) <= i->item->triggerRange)
		{
			i->die(state);
		}
		// Boomeroid hopping trigger
		else if (i->item->triggerType == TriggerType::Boomeroid &&
		         BattleUnitTileHelper::getDistanceStatic(position, i->position) <= BOOMEROID_RANGE)
		{
			i->hopTo(state, position);
		}
	}
}

void BattleUnit::startFalling()
{
	setMovementState(MovementState::None);
	falling = true;
}

void BattleUnit::requestGiveWay(const BattleUnit &requestor,
                                const std::list<Vec3<int>> &plannedPath, Vec3<int> pos)
{
	// If asked already or busy - cannot give way
	if (!giveWayRequestData.empty() || isBusy())
	{
		return;
	}
	// If unit is prone and we're trying to go into it's legs
	if (current_body_state == BodyState::Prone && tileObject->getOwningTile()->position != pos)
	{
		// Just ask unit to kneel for a moment
		giveWayRequestData.emplace_back(0, 0);
	}
	// If unit is not prone or we're trying to go into it's body
	else
	{
		static const std::map<Vec2<int>, int> facing_dir_map = {
		    {{0, -1}, 0}, {{1, -1}, 1}, {{1, 0}, 2},  {{1, 1}, 3},
		    {{0, 1}, 4},  {{-1, 1}, 5}, {{-1, 0}, 6}, {{-1, -1}, 7}};
		static const std::map<int, Vec2<int>> dir_facing_map = {
		    {0, {0, -1}}, {1, {1, -1}}, {2, {1, 0}},  {3, {1, 1}},
		    {4, {0, 1}},  {5, {-1, 1}}, {6, {-1, 0}}, {7, {-1, -1}}};

		// Start with unit's facing, and go to the sides, adding facings
		// if they're not in our path and not our current position.
		// Next facings: [0] is clockwise, [1] is counter-clockwise from current
		std::vector<int> nextFacings = {facing_dir_map.at(facing), facing_dir_map.at(facing)};
		for (int i = 0; i <= 4; i++)
		{
			int limit = i == 0 || i == 4 ? 0 : 1;
			for (int j = 0; j <= limit; j++)
			{
				auto nextFacing = dir_facing_map.at(nextFacings[j]);
				Vec3<int> nextPos = {position.x + nextFacing.x, position.y + nextFacing.y,
				                     position.z};
				if (nextPos == (Vec3<int>)requestor.position ||
				    std::find(plannedPath.begin(), plannedPath.end(), nextPos) != plannedPath.end())
				{
					continue;
				}
				giveWayRequestData.push_back(nextFacing);
			}
			nextFacings[0] = nextFacings[0] == 7 ? 0 : nextFacings[0] + 1;
			nextFacings[1] = nextFacings[1] == 0 ? 7 : nextFacings[1] - 1;
		}
	}
}

void BattleUnit::updateDisplayedItem()
{
	auto lastDisplayedItem = displayedItem;
	bool foundThrownItem = false;
	if (missions.size() > 0)
	{
		for (auto &m : missions)
		{
			if (m->type != BattleUnitMission::Type::ThrowItem || !m->item)
			{
				continue;
			}
			displayedItem = m->item->type;
			foundThrownItem = true;
			break;
		}
	}
	if (!foundThrownItem)
	{
		// If we're firing - try to keep last displayed item same, even if not dominant
		displayedItem = agent->getDominantItemInHands(
		    firing_animation_ticks_remaining > 0 ? lastDisplayedItem : nullptr);
	}
	// If displayed item changed or we are throwing - bring hands into "AtEase" state immediately
	if (foundThrownItem || displayedItem != lastDisplayedItem)
	{
		if (hand_animation_ticks_remaining > 0 || current_hand_state != HandState::AtEase)
		{
			setHandState(HandState::AtEase);
		}
	}
}

void BattleUnit::destroy(GameState &)
{
	this->tileObject->removeFromMap();
	this->shadowObject->removeFromMap();
	this->tileObject.reset();
	this->shadowObject.reset();
}

void BattleUnit::tryToRiseUp(GameState &state)
{
	// Do not rise up if unit is standing on us
	if (tileObject->getOwningTile()->getUnitIfPresent(true, true, false, tileObject))
		return;

	// Find state we can rise into (with animation)
	auto targetState = BodyState::Standing;
	while (targetState != BodyState::Downed &&
	       agent->getAnimationPack()->getFrameCountBody(displayedItem, current_body_state,
	                                                    targetState, current_hand_state,
	                                                    current_movement_state, facing) == 0)
	{
		switch (targetState)
		{
			case BodyState::Standing:
				if (agent->isBodyStateAllowed(BodyState::Flying))
				{
					targetState = BodyState::Flying;
					continue;
				}
			// Intentional fall-through
			case BodyState::Flying:
				if (agent->isBodyStateAllowed(BodyState::Kneeling))
				{
					targetState = BodyState::Kneeling;
					continue;
				}
			// Intentional fall-through
			case BodyState::Kneeling:
				if (canProne(position, facing))
				{
					targetState = BodyState::Prone;
					continue;
				}
			// Intentional fall-through
			case BodyState::Prone:
				// If we arrived here then we have no animation for standing up
				targetState = BodyState::Downed;
				continue;
			case BodyState::Downed:
			case BodyState::Jumping:
			case BodyState::Throwing:
				LogError("Not possible to reach this?");
				break;
		}
	}
	// Find state we can rise into (with no animation)
	if (targetState == BodyState::Downed)
	{
		if (agent->isBodyStateAllowed(BodyState::Standing))
		{
			targetState = BodyState::Standing;
		}
		else if (agent->isBodyStateAllowed(BodyState::Flying))
		{
			targetState = BodyState::Flying;
		}
		else if (agent->isBodyStateAllowed(BodyState::Kneeling))
		{
			targetState = BodyState::Kneeling;
		}
		else if (canProne(position, facing))
		{
			targetState = BodyState::Prone;
		}
		else
		{
			LogError("Unit cannot stand up???");
		}
	}

	missions.clear();
	addMission(state, BattleUnitMission::changeStance(*this, targetState));
}

void BattleUnit::dropDown(GameState &state)
{
	resetGoal();
	setMovementState(MovementState::None);
	setHandState(HandState::AtEase);
	setBodyState(state, target_body_state);
	// Check if we can drop from current state
	while (agent->getAnimationPack()->getFrameCountBody(displayedItem, current_body_state,
	                                                    BodyState::Downed, current_hand_state,
	                                                    current_movement_state, facing) == 0)
	{
		switch (current_body_state)
		{
			case BodyState::Jumping:
			case BodyState::Throwing:
			case BodyState::Flying:
				if (agent->isBodyStateAllowed(BodyState::Standing))
				{
					setBodyState(state, BodyState::Standing);
					continue;
				}
			// Intentional fall-through
			case BodyState::Standing:
				if (agent->isBodyStateAllowed(BodyState::Kneeling))
				{
					setBodyState(state, BodyState::Kneeling);
					continue;
				}
			// Intentional fall-through
			case BodyState::Kneeling:
				setBodyState(state, BodyState::Prone);
				continue;
			case BodyState::Prone:
			case BodyState::Downed:
				LogError("Not possible to reach this?");
				break;
		}
		break;
	}
	// Drop all gear
	while (!agent->equipment.empty())
	{
		addMission(state, BattleUnitMission::dropItem(*this, agent->equipment.front()));
	}
	// Drop gear used by missions
	std::list<sp<AEquipment>> itemsToDrop;
	for (auto &m : missions)
	{
		if (m->item && m->item->equippedSlotType != AEquipmentSlotType::None)
		{
			itemsToDrop.push_back(m->item);
		}
	}
	missions.clear();
	for (auto it : itemsToDrop)
	{
		addMission(state, BattleUnitMission::dropItem(*this, it));
	}
	addMission(state, BattleUnitMission::changeStance(*this, BodyState::Downed));
}

void BattleUnit::retreat(GameState &state)
{
	std::ignore = state;
	tileObject->removeFromMap();
	retreated = true;
	removeFromSquad(*state.current_battle);
	// FIXME: Trigger retreated event
}

void BattleUnit::die(GameState &state, bool violently, bool bledToDeath)
{
	if (violently)
	{
		// FIXME: Explode if nessecary, or spawn shit
		LogWarning("Implement violent deaths!");
	}
	// Clear focus
	for (auto u : focusedByUnits)
	{
		u->focusUnit.clear();
	}
	focusedByUnits.clear();
	// Emit sound
	if (agent->type->dieSfx.find(agent->gender) != agent->type->dieSfx.end() &&
	    !agent->type->dieSfx.at(agent->gender).empty())
	{
		fw().soundBackend->playSample(
		    listRandomiser(state.rng, agent->type->dieSfx.at(agent->gender)), position);
	}
	// FIXME: do what has to be done when unit dies
	LogWarning("Implement a UNIT DIED notification!");
	dropDown(state);
}

void BattleUnit::fallUnconscious(GameState &state)
{
	// FIXME: do what has to be done when unit goes unconscious
	dropDown(state);
}

void BattleUnit::beginBodyStateChange(GameState &state, BodyState bodyState)
{
	// Cease hand animation immediately
	if (hand_animation_ticks_remaining != 0)
		setHandState(target_hand_state);

	// Find which animation is possible
	int frameCount = agent->getAnimationPack()->getFrameCountBody(displayedItem, current_body_state,
	                                                              bodyState, current_hand_state,
	                                                              current_movement_state, facing);
	// No such animation
	// Try stopping movement
	if (frameCount == 0 && current_movement_state != MovementState::None)
	{
		frameCount = agent->getAnimationPack()->getFrameCountBody(displayedItem, current_body_state,
		                                                          bodyState, current_hand_state,
		                                                          MovementState::None, facing);
		if (frameCount != 0)
		{
			setMovementState(MovementState::None);
		}
	}
	// Try stopping aiming
	if (frameCount == 0 && current_hand_state != HandState::AtEase)
	{
		frameCount = agent->getAnimationPack()->getFrameCountBody(displayedItem, current_body_state,
		                                                          bodyState, HandState::AtEase,
		                                                          current_movement_state, facing);
		if (frameCount != 0)
		{
			setHandState(HandState::AtEase);
		}
	}

	int ticks = frameCount * TICKS_PER_FRAME_UNIT;
	if (ticks > 0 && current_body_state != bodyState)
	{
		target_body_state = bodyState;
		body_animation_ticks_remaining = ticks;
		// Updates bounds etc.
		if (tileObject)
		{
			setPosition(state, position);
		}
	}
	else
	{
		setBodyState(state, bodyState);
	}
}

void BattleUnit::setBodyState(GameState &state, BodyState bodyState)
{
	current_body_state = bodyState;
	target_body_state = bodyState;
	body_animation_ticks_remaining = 0;
	if (tileObject)
	{
		// Updates bounds etc.
		setPosition(state, position);
		// Update vision since our head position may have changed
		updateUnitVision(state);
	}
}

void BattleUnit::beginHandStateChange(HandState state)
{
	int frameCount = agent->getAnimationPack()->getFrameCountHands(
	    displayedItem, current_body_state, current_hand_state, state, current_movement_state,
	    facing);
	int ticks = frameCount * TICKS_PER_FRAME_UNIT;

	if (ticks > 0 && current_hand_state != state)
	{
		target_hand_state = state;
		hand_animation_ticks_remaining = ticks;
	}
	else
	{
		setHandState(state);
	}
	aiming_ticks_remaining = 0;
}

void BattleUnit::setHandState(HandState state)
{
	current_hand_state = state;
	target_hand_state = state;
	hand_animation_ticks_remaining = 0;
	firing_animation_ticks_remaining =
	    state != HandState::Firing
	        ? 0
	        : agent->getAnimationPack()->getFrameCountFiring(displayedItem, current_body_state,
	                                                         current_movement_state, facing) *
	              TICKS_PER_FRAME_UNIT;
	aiming_ticks_remaining = state == HandState::Aiming ? TICKS_PER_SECOND / 3 : 0;
}

void BattleUnit::beginTurning(GameState &, Vec2<int> newFacing)
{
	goalFacing = newFacing;
	turning_animation_ticks_remaining = TICKS_PER_FRAME_UNIT;
}

void BattleUnit::setFacing(GameState &state, Vec2<int> newFacing)
{
	facing = newFacing;
	goalFacing = newFacing;
	turning_animation_ticks_remaining = 0;
	updateUnitVision(state);
}

void BattleUnit::setMovementState(MovementState state)
{
	current_movement_state = state;
	switch (state)
	{
		case MovementState::None:
			movement_ticks_passed = 0;
			movement_sounds_played = 0;
			ticksTillNextTargetCheck = 0;
			break;
		case MovementState::Running:
		case MovementState::Strafing:
			if (current_hand_state != HandState::AtEase || target_hand_state != HandState::AtEase)
			{
				setHandState(HandState::AtEase);
			}
			break;
		default:
			break;
	}
}

unsigned int BattleUnit::getWalkSoundIndex()
{
	if (current_movement_state == MovementState::Running)
	{
		return ((movement_sounds_played + UNITS_TRAVELLED_PER_SOUND_RUNNING_DIVISOR - 1) /
		        UNITS_TRAVELLED_PER_SOUND_RUNNING_DIVISOR) %
		       2;
	}
	else
	{
		return movement_sounds_played % 2;
	}
}

Vec3<float> BattleUnit::getMuzzleLocation() const
{
	return position +
	       Vec3<float>{0.0f, 0.0f,
	                   ((float)agent->type->bodyType->muzzleZPosition.at(current_body_state)) /
	                       40.0f};
}

Vec3<float> BattleUnit::getThrownItemLocation() const
{
	return position +
	       Vec3<float>{0.0f, 0.0f,
	                   ((float)agent->type->bodyType->height.at(BodyState::Throwing) - 4.0f) /
	                       2.0f / 40.0f};
}

bool BattleUnit::shouldPlaySoundNow()
{
	bool play = false;
	unsigned int sounds_to_play = getDistanceTravelled() / UNITS_TRAVELLED_PER_SOUND;
	if (sounds_to_play != movement_sounds_played)
	{
		unsigned int divisor = (current_movement_state == MovementState::Running)
		                           ? UNITS_TRAVELLED_PER_SOUND_RUNNING_DIVISOR
		                           : 1;
		play = ((sounds_to_play + divisor - 1) % divisor) == 0;
		movement_sounds_played = sounds_to_play;
	}
	return play;
}

bool BattleUnit::popFinishedMissions(GameState &state)
{
	while (missions.size() > 0 && missions.front()->isFinished(state, *this))
	{
		LogWarning("Unit mission \"%s\" finished", missions.front()->getName().cStr());
		missions.pop_front();

		// We may have retreated as a result of finished mission
		if (retreated)
			return true;

		if (!missions.empty())
		{
			missions.front()->start(state, *this);
			continue;
		}
		else
		{
			LogWarning("No next unit mission, going idle");
			break;
		}
	}
	return false;
}

bool BattleUnit::getNextDestination(GameState &state, Vec3<float> &dest)
{
	if (missions.empty())
	{
		return false;
	}
	return missions.front()->getNextDestination(state, *this, dest);
}

bool BattleUnit::getNextFacing(GameState &state, Vec2<int> &dest)
{
	if (missions.empty())
	{
		return false;
	}
	return missions.front()->getNextFacing(state, *this, dest);
}

bool BattleUnit::getNextBodyState(GameState &state, BodyState &dest)
{
	if (missions.empty())
	{
		return false;
	}
	return missions.front()->getNextBodyState(state, *this, dest);
}

bool BattleUnit::addMission(GameState &state, BattleUnitMission::Type type)
{
	switch (type)
	{
		case BattleUnitMission::Type::RestartNextMission:
			return addMission(state, BattleUnitMission::restartNextMission(*this));
		case BattleUnitMission::Type::ReachGoal:
			return addMission(state, BattleUnitMission::reachGoal(*this));
		case BattleUnitMission::Type::ThrowItem:
		case BattleUnitMission::Type::Snooze:
		case BattleUnitMission::Type::ChangeBodyState:
		case BattleUnitMission::Type::Turn:
		case BattleUnitMission::Type::AcquireTU:
		case BattleUnitMission::Type::GotoLocation:
		case BattleUnitMission::Type::Teleport:
			LogError("Cannot add mission by type if it requires parameters");
			break;
	}
	return false;
}

bool BattleUnit::cancelMissions(GameState &state)
{
	if (popFinishedMissions(state))
	{
		// Unit retreated
		return false;
	}
	if (missions.empty())
	{
		return true;
	}

	// Figure out if we can cancel the mission in front
	bool letFinish = false;
	switch (missions.front()->type)
	{
		// Missions that cannot be cancelled
		case BattleUnitMission::Type::ThrowItem:
			return false;
		// Missions that must be let finish (unless forcing)
		case BattleUnitMission::Type::ChangeBodyState:
		case BattleUnitMission::Type::Turn:
		case BattleUnitMission::Type::GotoLocation:
		case BattleUnitMission::Type::ReachGoal:
			letFinish = true;
			break;
		// Missions that can be cancelled
		case BattleUnitMission::Type::Snooze:
		case BattleUnitMission::Type::DropItem:
		case BattleUnitMission::Type::Teleport:
		case BattleUnitMission::Type::RestartNextMission:
		case BattleUnitMission::Type::AcquireTU:
			break;
	}

	// Figure out what to do with the unfinished mission
	if (letFinish)
	{
		auto &m = missions.front();
		// If turning - downgrade to a turning mission
		if (facing != goalFacing)
		{
			m->type = BattleUnitMission::Type::Turn;
			m->targetFacing = goalFacing;
			if (m->costPaidUpFront > 0)
			{
				// Refund queued action, subtract turning cost
				agent->modified_stats.time_units += m->costPaidUpFront - 1;
			}
		}
		// If changing body - downgrade to a body state change mission
		else if (current_body_state != target_body_state)
		{
			m->type = BattleUnitMission::Type::ChangeBodyState;
			m->targetBodyState = target_body_state;
		}
		else
		{
			letFinish = false;
		}
	}

	// Cancel missions
	while (missions.size() > (letFinish ? 1 : 0))
	{
		agent->modified_stats.time_units += missions.back()->costPaidUpFront;
		missions.pop_back();
	}
	if (missions.empty() && !atGoal)
	{
		addMission(state, BattleUnitMission::Type::ReachGoal);
	}
	return true;
}

bool BattleUnit::setMission(GameState &state, BattleUnitMission *mission)
{
	// Check if mission was actually passed
	// We can receive nullptr here in case mission was impossible
	if (!mission)
	{
		return false;
	}

	// Special checks and actions based on mission type
	switch (mission->type)
	{
		case BattleUnitMission::Type::Turn:
			stopAttacking();
			break;
		case BattleUnitMission::Type::ThrowItem:
			// We already checked if item is throwable inside the mission creation
			break;
	}

	if (!cancelMissions(state))
	{
		return false;
	}

	// There is a mission remaining that wants to let it finish
	if (!missions.empty())
	{
		switch (mission->type)
		{
			// Instant throw always cancels if agent can afford it
			case BattleUnitMission::Type::ThrowItem:
			{
				// FIXME: actually read the option
				bool USER_OPTION_ALLOW_INSTANT_THROWS = false;
				if (USER_OPTION_ALLOW_INSTANT_THROWS &&
				    canAfford(state, BattleUnitMission::getThrowCost(*this)))
				{
					setBodyState(state, current_body_state);
					setFacing(state, facing);
					missions.clear();
				}
				break;
			}
			// Turning can be cancelled if our mission will require us to turn in a different dir
			// Also reachGoal can be cancelled by GotoLocation
			case BattleUnitMission::Type::Turn:
			case BattleUnitMission::Type::GotoLocation:
			case BattleUnitMission::Type::ReachGoal:
			{
				if (missions.front()->type == BattleUnitMission::Type::ReachGoal &&
				    mission->type == BattleUnitMission::Type::GotoLocation)
				{
					missions.clear();
				}
				else if (facing != goalFacing)
				{
					Vec2<int> nextFacing;
					bool haveNextFacing = true;
					switch (mission->type)
					{
						case BattleUnitMission::Type::Turn:
							nextFacing =
							    BattleUnitMission::getFacingStep(*this, mission->targetFacing);
							break;
						case BattleUnitMission::Type::GotoLocation:
							// We have to start it in order to see where we're going
							mission->start(state, *this);
							if (mission->currentPlannedPath.empty())
							{
								haveNextFacing = false;
								break;
							}
							nextFacing = BattleUnitMission::getFacingStep(
							    *this, BattleUnitMission::getFacing(
							               *this, mission->currentPlannedPath.front()));
							break;
						case BattleUnitMission::Type::ReachGoal:
							nextFacing = BattleUnitMission::getFacingStep(
							    *this, BattleUnitMission::getFacing(*this, position, goalPosition));
							break;
						default: // don't cry about unhandled case, compiler
							break;
					}
					// If we are turning towards something that will not be our next facing when we
					// try
					// to execute our mission then we're better off canceling it
					if (haveNextFacing && nextFacing != goalFacing)
					{
						setFacing(state, facing);
						missions.clear();
					}
				}
				break;
			}
			default:
				break;
		}
	}

	// Finally, add the mission
	return addMission(state, mission);
}

bool BattleUnit::addMission(GameState &state, BattleUnitMission *mission, bool toBack)
{
	if (toBack)
	{
		missions.emplace_back(mission);
		return true;
	}

	switch (mission->type)
	{
		// Reach goal can only be added if it can overwrite the mission
		case BattleUnitMission::Type::ReachGoal:
		{
			if (missions.size() > 0)
			{
				switch (missions.front()->type)
				{
					// Missions that prevent going to goal
					case BattleUnitMission::Type::Snooze:
					case BattleUnitMission::Type::ThrowItem:
					case BattleUnitMission::Type::ChangeBodyState:
					case BattleUnitMission::Type::ReachGoal:
					case BattleUnitMission::Type::DropItem:
					case BattleUnitMission::Type::Teleport:
					case BattleUnitMission::Type::RestartNextMission:
					case BattleUnitMission::Type::GotoLocation:
					case BattleUnitMission::Type::Turn:
						return false;
					// Missions that can be overwritten
					case BattleUnitMission::Type::AcquireTU:
						break;
				}
			}
			missions.emplace_front(mission);
			mission->start(state, *this);
			break;
		}
		// Missions that can be added to the back at any time
		case BattleUnitMission::Type::Turn:
		case BattleUnitMission::Type::ChangeBodyState:
		case BattleUnitMission::Type::ThrowItem:
		case BattleUnitMission::Type::GotoLocation:
		case BattleUnitMission::Type::Teleport:
			missions.emplace_back(mission);
			// Missions added to back normally start only if they are the only mission in the queue
			// Teleport always starts immediately, even if the agent is waiting to finish something
			if (missions.size() == 1 || mission->type == BattleUnitMission::Type::Teleport)
			{
				mission->start(state, *this);
			}
			break;
		// Missions that can be added to the front at any time
		case BattleUnitMission::Type::Snooze:
		case BattleUnitMission::Type::AcquireTU:
		case BattleUnitMission::Type::RestartNextMission:
		case BattleUnitMission::Type::DropItem:
			missions.emplace_front(mission);
			mission->start(state, *this);
			break;
	}
	return true;
}

Vec3<int> rotate(Vec3<int> vec, int rotation)
{
	switch (rotation)
	{
		case 1:
			return {-vec.y, vec.x, vec.z};
		case 2:
			return {-vec.x, -vec.y, vec.z};
		case 3:
			return {vec.y, -vec.x, vec.z};
		default:
			return vec;
	}
}

void BattleUnit::groupMove(GameState &state, std::list<StateRef<BattleUnit>> &selectedUnits,
                           Vec3<int> targetLocation, bool demandGiveWay)
{
	// Legend:
	//
	// (arrive from the southwest)						(arrive from the south)
	//
	//         6			G = goal					         7			G = goal
	//       5   6			F = flanks					       7   7		1 = 1s back row
	//     4   5   6		1 = 1st back row			     6   6   6		2 = 2nd back row
	//   3   F   5   6		2 = 2nd back row			   5   5   5   5	3 = 3rd back row
	// 2   1   G   5   6	3 = sides of 1st back row	 F   F   G   F   F	4 = sides of 1st bk row
	//   2   1   F   5		4 = sides of flanks			   4   1   1   4	F = flanks
	//     2   1   4		5 = 1st front row			     2   2   2		5 = 1st front row
	//       2   3			6 = 2nd front row			       3   3		6 = 2nd front row
	//         2										         3			7 = 3rd front row
	//
	// We will of course rotate this accordingly with the direction from which units come

	static const std::list<Vec3<int>> targetOffsetsDiagonal = {
	    // Two locations to the flanks
	    {-1, -1, 0},
	    {1, 1, 0},
	    // Three locations in the 1st back row
	    {-1, 1, 0},
	    {-2, 0, 0},
	    {0, 2, 0},
	    // 2nd Back row
	    {-2, 2, 0},
	    {-3, 1, 0},
	    {-1, 3, 0},
	    {-4, 0, 0},
	    {0, 4, 0},
	    // Two locations to the side of the 1st back row
	    {-3, -1, 0},
	    {1, 3, 0},
	    // Two locations to the side of the flanks
	    {-2, -2, 0},
	    {2, 2, 0},
	    // 1st Front row
	    {1, -1, 0},
	    {0, -2, 0},
	    {2, 0, 0},
	    {-1, -3, 0},
	    {3, 1, 0},
	    // 2nd Front row
	    {2, -2, 0},
	    {1, -3, 0},
	    {3, -1, 0},
	    {0, -4, 0},
	    {4, 0, 0},
	};
	static const std::map<Vec2<int>, int> rotationDiagonal = {
	    {{1, -1}, 0}, {{1, 1}, 1}, {{-1, 1}, 2}, {{-1, -1}, 3},
	};
	static const std::list<Vec3<int>> targetOffsetsLinear = {
	    // Two locations in the 1st back row
	    {-1, 1, 0},
	    {1, 1, 0},
	    // Three locations in the 2nd back row
	    {0, 2, 0},
	    {-2, 2, 0},
	    {2, 2, 0},
	    // 3rd Back row
	    {-1, 3, 0},
	    {1, 3, 0},
	    {0, 4, 0},
	    // Sides of the 1st back row
	    {-3, 1, 0},
	    {3, 1, 0},
	    // Flanks
	    {-2, 0, 0},
	    {2, 0, 0},
	    {-4, 0, 0},
	    {4, 0, 0},
	    // 1st front row
	    {-1, -1, 0},
	    {1, -1, 0},
	    {-3, -1, 0},
	    {3, -1, 0},
	    // 2nd front row
	    {0, -2, 0},
	    {-2, -2, 0},
	    {2, -2, 0},
	    // 3rd front row
	    {-1, -3, 0},
	    {1, -3, 0},
	    {0, -4, 0},
	};
	static const std::map<Vec2<int>, int> rotationLinear = {
	    {{0, -1}, 0}, {{1, 0}, 1}, {{0, 1}, 2}, {{-1, 0}, 3},
	};

	if (selectedUnits.empty())
	{
		return;
	}

	UString log = ";";
	log += format("\nGroup move order issued to %d, %d, %d. Looking for the leader. Total number "
	              "of units: %d",
	              targetLocation.x, targetLocation.y, targetLocation.z, (int)selectedUnits.size());

	// Sort units based on proximity to target and speed

	auto &map = selectedUnits.front()->tileObject->map;
	auto units = selectedUnits;
	units.sort([targetLocation](const StateRef<BattleUnit> &a, const StateRef<BattleUnit> &b) {
		return BattleUnitTileHelper::getDistanceStatic((Vec3<int>)a->position, targetLocation) /
		           a->agent->modified_stats.getActualSpeedValue() <
		       BattleUnitTileHelper::getDistanceStatic((Vec3<int>)b->position, targetLocation) /
		           b->agent->modified_stats.getActualSpeedValue();
	});

	// Find the unit that will lead the group

	StateRef<BattleUnit> leadUnit;
	BattleUnitMission *leadMission = nullptr;
	int minDistance = INT_MAX;
	auto itUnit = units.begin();
	while (itUnit != units.end())
	{
		auto curUnit = *itUnit;
		log += format("\nTrying unit %s for leader", curUnit.id);

		auto mission = BattleUnitMission::gotoLocation(*curUnit, targetLocation);
		bool missionAdded = curUnit->setMission(state, mission);
		if (missionAdded)
		{
			mission->start(state, *curUnit);
			if (!mission->currentPlannedPath.empty())
			{
				auto unitTarget = mission->currentPlannedPath.back();
				int absX = std::abs(targetLocation.x - unitTarget.x);
				int absY = std::abs(targetLocation.y - unitTarget.y);
				int absZ = std::abs(targetLocation.z - unitTarget.z);
				int distance = std::max(std::max(absX, absY), absZ) + absX + absY + absZ;
				if (distance < minDistance)
				{
					log += format("\nUnit was the closest to target yet, remembering him.");
					// Cancel last leader's mission
					if (leadMission)
					{
						leadMission->cancelled = true;
					}
					minDistance = distance;
					leadUnit = curUnit;
					leadMission = mission;
				}
				if (distance == 0)
				{
					log += format("\nUnit could reach target, chosen to be the leader.");
					break;
				}
			}
		}
		mission->cancelled = true;
		if (missionAdded)
		{
			log += format("\nUnit could not path to target, trying next one.");
			// Unit cannot path to target but maybe he can path to something near it, leave him in
			itUnit++;
		}
		else
		{
			log += format("\nUnit could not set mission, removing.");
			// Unit cannot add a movement mission - remove him
			itUnit = units.erase(itUnit);
		}
	}
	if (itUnit == units.end() && !leadUnit)
	{
		log += format("\nNoone could path to target, aborting");
		LogWarning("%s", log.cStr());
		return;
	}

	// In case we couldn't reach it, change our target
	targetLocation = leadMission->currentPlannedPath.back();
	// Remove leader from list of units that require pathing
	units.remove(leadUnit);
	// Determine our direction and rotation
	auto fromIt = leadMission->currentPlannedPath.rbegin();
	int fromLimit = std::min(3, (int)leadMission->currentPlannedPath.size());
	for (int i = 0; i < fromLimit; i++)
	{
		fromIt++;
	}
	Vec2<int> dir = {clamp(targetLocation.x - fromIt->x, -1, 1),
	                 clamp(targetLocation.y - fromIt->y, -1, 1)};
	if (dir.x == 0 && dir.y == 0)
	{
		dir.y = -1;
	}
	bool diagonal = dir.x != 0 && dir.y != 0;
	auto &targetOffsets = diagonal ? targetOffsetsDiagonal : targetOffsetsLinear;
	int rotation = diagonal ? rotationDiagonal.at(dir) : rotationLinear.at(dir);

	// Sort remaining units based on proximity to target and speed
	auto h = BattleUnitTileHelper(map, *leadUnit);
	units.sort([h, targetLocation](const StateRef<BattleUnit> &a, const StateRef<BattleUnit> &b) {
		return h.getDistance((Vec3<int>)a->position, targetLocation) /
		           a->agent->modified_stats.getActualSpeedValue() <
		       h.getDistance((Vec3<int>)b->position, targetLocation) /
		           b->agent->modified_stats.getActualSpeedValue();
	});

	// Path every other unit to areas around target
	log += format("\nTarget location is now %d, %d, %d. Leader is %s", targetLocation.x,
	              targetLocation.y, targetLocation.z, leadUnit.id);

	auto itOffset = targetOffsets.begin();
	for (auto unit : units)
	{
		if (itOffset == targetOffsets.end())
		{
			log += format("\nRan out of location offsets, exiting");
			LogWarning("%s", log.cStr());
			return;
		}
		log += format("\nPathing unit %s", unit.id);
		while (itOffset != targetOffsets.end())
		{
			auto offset = rotate(*itOffset, rotation);
			auto targetLocationOffsetted = targetLocation + offset;
			if (targetLocationOffsetted.x < 0 || targetLocationOffsetted.x >= map.size.x ||
			    targetLocationOffsetted.y < 0 || targetLocationOffsetted.y >= map.size.y ||
			    targetLocationOffsetted.z < 0 || targetLocationOffsetted.z >= map.size.z)
			{
				log += format("\nLocation was outside map bounds, trying next one");
				itOffset++;
				continue;
			}

			log += format("\nTrying location %d, %d, %d at offset %d, %d, %d",
			              targetLocationOffsetted.x, targetLocationOffsetted.y,
			              targetLocationOffsetted.z, offset.x, offset.y, offset.z);
			float costLimit =
			    1.50f * 2.0f * (float)(std::max(std::abs(offset.x), std::abs(offset.y)) +
			                           std::abs(offset.x) + std::abs(offset.y));
			auto path = map.findShortestPath(targetLocation, targetLocationOffsetted,
			                                 costLimit / 2.0f, h, true, nullptr, costLimit);
			itOffset++;
			if (!path.empty() && path.back() == targetLocationOffsetted)
			{
				log += format("\nLocation checks out, pathing to it");
				unit->setMission(state,
				                 BattleUnitMission::gotoLocation(*unit, targetLocationOffsetted));
				break;
			}
			log += format("\nLocation was unreachable, trying next one");
		}
	}
	log += format("\nSuccessfully pathed everybody to target");
	LogWarning("%s", log.cStr());
}
}
