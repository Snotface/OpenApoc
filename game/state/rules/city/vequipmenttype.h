#pragma once

#include "game/state/shared/equipment.h"
#include "game/state/stateobject.h"
#include "library/sp.h"
#include "library/strings.h"
#include "library/vec.h"
#include <list>
#include <map>
#include <set>

namespace OpenApoc
{

class Rules;
class Image;
class Sample;
class DoodadType;
class Organisation;
class VAmmoType;

class VEquipmentType : public StateObject
{
	STATE_OBJECT(VEquipmentType)
  public:
	VEquipmentType();

	enum class User
	{
		Ground,
		Air,
		Ammo,
	};

	~VEquipmentType() override = default;

	// Shared stuff
	EquipmentSlotType type;
	UString id;
	UString name;
	int weight;
	int max_ammo;
	StateRef<VAmmoType> ammo_type;
	sp<Image> equipscreen_sprite;
	Vec2<int> equipscreen_size;
	StateRef<Organisation> manufacturer;
	int store_space;
	std::set<User> users;

	// Weapons
	int speed;
	std::list<sp<Image>> projectile_sprites; // A list of sprites forming the projectile
	                                         // 'bullet'/'beam' - 'nullptr' gaps are expected
	int damage;
	int accuracy;
	// Fire delay, in ticks, to fire a shot
	// For some reason, it is halved in the vanilla data files.
	// Therefore, to get vanilla ticks, it must be multiplied by 2, and to get OpenApoc ticks, by 8
	int fire_delay;
	int tail_size;
	bool guided;
	// How much can it turn per tick.
	// Based on the fact that retribution (tr = 10) turns 90 degrees (PI/2) per second
	// One point of turn rate is equal to PI/20 turned per second
	int turn_rate;
	int range;
	int ttl;
	int firing_arc_1;
	int firing_arc_2;
	bool point_defence;
	sp<Sample> fire_sfx;
	sp<Sample> impact_sfx;
	StateRef<DoodadType> explosion_graphic;
	sp<Image> icon;

	// Engine stuff
	int power;
	int top_speed;

	// Other ('general') equipment stuff
	int accuracy_modifier;
	int cargo_space;
	int passengers;
	int alien_space;
	int missile_jamming;
	int shielding;
	bool cloaking;
	bool teleporting;
};

} // namespace OpenApoc
