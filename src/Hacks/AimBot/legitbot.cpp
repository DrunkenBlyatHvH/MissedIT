#include "legitbot.h"
#include "autowall.h"
#include "aimbot.hpp"

bool Legitbot::aimStepInProgress = false;
std::vector<int64_t> Legitbot::friends = { };
std::vector<long> killTimes = { 0 }; // the Epoch time from when we kill someone

QAngle AimStepLastAngle;
QAngle LastPunch;
static bool shouldAim = false;
int Legitbot::targetAimbot = -1;

C_BasePlayer* Legitbot::GetClosestEnemy (C_BasePlayer *localplayer, const LegitWeapon_t& currentSettings)
{
	if (!localplayer || !localplayer->GetAlive())
		return nullptr;

	float bestFov = currentSettings.LegitautoAimFov;

	Vector pVecTarget = localplayer->GetEyePosition();
	C_BasePlayer* enemy = nullptr;
	QAngle viewAngles;
		engine->GetViewAngles(viewAngles);

	int maxClient = engine->GetMaxClients();
	for (int i = maxClient; i > 1 ; i--)
	{
		C_BasePlayer* player = (C_BasePlayer*)entityList->GetClientEntity(i);

		if (!player
	    	|| player == localplayer
	    	|| player->GetDormant()
	    	|| !player->GetAlive()
	    	|| player->GetImmune())
	    	continue;

		if (Entity::IsTeamMate(player, localplayer))
	   	 	continue;

		Vector cbVecTarget = player->GetEyePosition();
		
		float cbFov = Math::GetFov( viewAngles, Math::CalcAngle(pVecTarget, cbVecTarget) );
		if (cbFov > bestFov)
			continue;
	
		bestFov = cbFov;
		enemy = player;
	}

	return enemy;
}

void Legitbot::GetClosestSpot(C_BasePlayer* localplayer, C_BasePlayer* enemy, Vector &BestSpot, const LegitWeapon_t& currentSettings)
{
	if (!localplayer || !localplayer->GetAlive())
		return;
	if (!enemy || !enemy->GetAlive())
		return;

	const std::unordered_map<int, int> *modelType = BoneMaps::GetModelTypeBoneMap(enemy);

	float bestFov = currentSettings.LegitautoAimFov;

	int len = 31;

	int PrevDamage = 0;

	for( int i = 0; i < len; i++ )
	{
		int boneID = (*modelType).at(i);

		if( boneID == BONE_INVALID )
			continue;

		Vector bone3D = enemy->GetBonePosition( boneID );

		if (!currentSettings.mindamage)
		{
			Vector pVecTarget = localplayer->GetEyePosition();
			QAngle viewAngles;
				engine->GetViewAngles(viewAngles);

			Vector cbVecTarget = bone3D;
			float cbFov = Math::GetFov( viewAngles, Math::CalcAngle(pVecTarget, cbVecTarget) );
			
			if (cbFov < bestFov)
			{
				bestFov = cbFov;
				BestSpot = bone3D;
			}
			continue;
		}
		
		if (!IsInFov(localplayer, enemy, bone3D, currentSettings))	continue;

		int boneDamage = AutoWall::GetDamage(bone3D, true);

		if (boneDamage >= enemy->GetHealth()){
			BestSpot = bone3D;
			return;
		}
		else if ( (boneDamage > PrevDamage && boneDamage >= currentSettings.minDamagevalue) )
		{	
			BestSpot = bone3D;
			PrevDamage = boneDamage;
		}
		
		if (boneDamage >= 70 && i >= BONE_HEAD)
			break;
	}
}

C_BasePlayer* Legitbot::GetClosestPlayerAndSpot(CUserCmd* cmd, C_BasePlayer* localplayer, bool visibleCheck, Vector& bestSpot, float& bestDamage, const LegitWeapon_t& currentSettings)
{
	if (!currentSettings.autoAimEnabled) return nullptr;
	if (!localplayer || !localplayer->GetAlive() )	return nullptr;
	if ( localplayer->IsFlashed() )	return nullptr;

	C_BasePlayer *player = nullptr;
	player = Legitbot::GetClosestEnemy(localplayer, currentSettings); // getting the closest enemy to the crosshair

	if ( !player || !player->GetAlive())	return nullptr;

	int BoneId = (int)(currentSettings.bone);

	const std::unordered_map<int, int> *modelType = BoneMaps::GetModelTypeBoneMap(player);

	BoneId = (*modelType).at(BoneId);
	Vector bone3d = player->GetBonePosition( BoneId );

	GetClosestSpot(localplayer, player, bone3d, currentSettings);
	
	if ( LineGoesThroughSmoke( localplayer->GetEyePosition( ), bone3d, true ) )
		return nullptr;

	bestSpot = bone3d;

	if (AutoWall::GetDamage(bone3d, true) <= 0 || !Entity::IsSpotVisible(player, bone3d))
		return nullptr;

	return player;
}

bool Legitbot::IsInFov(C_BasePlayer* localplayer, C_BasePlayer* player, const Vector &spot, const LegitWeapon_t& weaponSettings)
{
	if (!localplayer || !localplayer->GetAlive()) return false; 
	if (!player || !player->GetAlive())	return false;

	Vector pVecTarget = localplayer->GetEyePosition();
	QAngle viewAngles;
		engine->GetViewAngles(viewAngles);

	float cbFov = Math::GetFov( viewAngles, Math::CalcAngle(pVecTarget, spot) );
	if (cbFov > weaponSettings.LegitautoAimFov)
		return false;
	
	return true;
}

void Legitbot::AimStep(C_BasePlayer* player, QAngle& angle, CUserCmd* cmd, bool& shouldAim, const LegitWeapon_t& currentSettings)
{
	if (!currentSettings.aimStepEnabled)
		return;

	if (!currentSettings.autoAimEnabled)
		return;

	if (currentSettings.smoothEnabled)
		return;

	if (!shouldAim)
		return;

	if (!Legitbot::aimStepInProgress)
		AimStepLastAngle = cmd->viewangles;

	if (!player || !player->GetAlive())
		return;

	float fov = Math::GetFov(AimStepLastAngle, angle);

	Legitbot::aimStepInProgress = ( fov > (Math::float_rand(currentSettings.aimStepMin, currentSettings.aimStepMax)) );

	if (!Legitbot::aimStepInProgress)
		return;

    cmd->buttons &= ~(IN_ATTACK); // aimstep in progress, don't shoot.

	QAngle deltaAngle = AimStepLastAngle - angle;

	Math::NormalizeAngles(deltaAngle);
	float randX = Math::float_rand(currentSettings.aimStepMin, std::min(currentSettings.aimStepMax, fov));
	float randY = Math::float_rand(currentSettings.aimStepMin, std::min(currentSettings.aimStepMax, fov));
	if (deltaAngle.y < 0)
		AimStepLastAngle.y += randY;
	else
		AimStepLastAngle.y -= randY;

	if(deltaAngle.x < 0)
		AimStepLastAngle.x += randX;
	else
		AimStepLastAngle.x -= randX;

	angle = AimStepLastAngle;
}

void Legitbot::Salt(float& smooth, const LegitWeapon_t& currentSettings)
{
	float sine = sin (globalVars->tickcount);
	float salt = sine * currentSettings.smoothSaltMultiplier;
	float oval = smooth + salt;
	smooth *= oval;
}

void Legitbot::Smooth(C_BasePlayer* player, QAngle& angle, bool& shouldAim, const LegitWeapon_t& currentSettings)
{
	if (!currentSettings.smoothEnabled)
		return;
	if (!shouldAim || !player || !player->GetAlive())
		return;

	QAngle viewAngles;
		engine->GetViewAngles(viewAngles);

	QAngle delta = angle - viewAngles;
	Math::NormalizeAngles(delta);

	float smooth = powf(currentSettings.smoothAmount, 0.4f); // Makes more slider space for actual useful values

	smooth = std::min(0.99f, smooth);

	if (currentSettings.smoothSaltEnabled)
		Legitbot::Salt(smooth, currentSettings);

	QAngle toChange = {0,0,0};

	SmoothType type = currentSettings.smoothType;

	if (type == SmoothType::SLOW_END)
		toChange = delta - (delta * smooth);
	else if (type == SmoothType::CONSTANT || type == SmoothType::FAST_END)
	{
		float coeff = (1.0f - smooth) / delta.Length() * 4.f;

		if (type == SmoothType::FAST_END)
			coeff = powf(coeff, 2.f) * 10.f;

		coeff = std::min(1.f, coeff);
		toChange = delta * coeff;
	}

	angle = viewAngles + toChange;
}

void Legitbot::AutoShoot(C_BasePlayer* localplayer, C_BaseCombatWeapon* activeWeapon, CUserCmd* cmd, LegitWeapon_t* currentSettings)
{
    if (!currentSettings->autoShoot)
		return;
	if (!activeWeapon || activeWeapon->GetInReload())
		return;
	
	if (cmd->buttons & IN_USE)
		return;
	
	if (currentSettings->autoScopeEnabled && Util::Items::IsScopeable(*activeWeapon->GetItemDefinitionIndex()) && !localplayer->IsScoped() && !(cmd->buttons & IN_ATTACK2) && !(cmd->buttons&IN_ATTACK) )
	{
		cmd->buttons |= IN_ATTACK2;
	}

    CSWeaponType weaponType = activeWeapon->GetCSWpnData()->GetWeaponType();
    if (weaponType == CSWeaponType::WEAPONTYPE_KNIFE || weaponType == CSWeaponType::WEAPONTYPE_C4 || weaponType == CSWeaponType::WEAPONTYPE_GRENADE)
		return;
	if (*activeWeapon->GetItemDefinitionIndex() == ItemDefinitionIndex::WEAPON_REVOLVER)
		return;
		
	if ( Aimbot::canShoot(localplayer, activeWeapon, currentSettings->hitchance) )
	{
		if (activeWeapon->GetNextPrimaryAttack() > globalVars->curtime)
			cmd->buttons &= ~IN_ATTACK;
		else
			cmd->buttons |= IN_ATTACK;	
		return;
	}	

	Aimbot::AutoSlow(localplayer, cmd, currentSettings->autoSlow);
}

void Legitbot::CreateMove(CUserCmd* cmd)
{
	C_BasePlayer* localplayer = (C_BasePlayer*) entityList->GetClientEntity(engine->GetLocalPlayer());
	if (!localplayer || !localplayer->GetAlive())
		return;
	C_BaseCombatWeapon* activeWeapon = (C_BaseCombatWeapon*) entityList->GetClientEntityFromHandle(localplayer->GetActiveWeapon());
	if (!activeWeapon || activeWeapon->GetInReload() || activeWeapon->GetAmmo() == 0)
		return;

	if (activeWeapon->GetNextPrimaryAttack() > globalVars->curtime)
		return;

	ItemDefinitionIndex index = ItemDefinitionIndex::INVALID;
	if (Settings::Legitbot::weapons.find(*activeWeapon->GetItemDefinitionIndex()) != Settings::Legitbot::weapons.end())
		index = *activeWeapon->GetItemDefinitionIndex();
	currentWeaponSetting = &Settings::Legitbot::weapons.at(index);

	static QAngle oldAngle;
	static QAngle angle; 

	angle = cmd->viewangles;
	engine->GetViewAngles(oldAngle);	

	
	static QAngle lastRandom = QAngle(0);
	Vector localEye = localplayer->GetEyePosition();

	if (currentWeaponSetting->ignoreJumpEnabled && (!(localplayer->GetFlags() & FL_ONGROUND) && localplayer->GetMoveType() != MOVETYPE_LADDER))
		return;

	CSWeaponType weaponType = activeWeapon->GetCSWpnData()->GetWeaponType();
	if (weaponType == CSWeaponType::WEAPONTYPE_C4 || weaponType == CSWeaponType::WEAPONTYPE_GRENADE || weaponType == CSWeaponType::WEAPONTYPE_KNIFE)
		return;

    Vector bestSpot = Vector(0);
	float bestDamage = float(0);

	C_BasePlayer* player = Legitbot::GetClosestPlayerAndSpot(cmd, localplayer, true, bestSpot, bestDamage, *currentWeaponSetting);

	if (player)
	{
		if ( cmd->buttons & IN_ATTACK )
			shouldAim = true;
		else if (currentWeaponSetting->aimkeyOnly && inputSystem->IsButtonDown(currentWeaponSetting->aimkey))
			shouldAim = true;
		else if ( currentWeaponSetting->autoShoot )
			shouldAim = true;
		else
			shouldAim = false;
		
		
		Settings::Debug::AutoAim::target = bestSpot; // For Debug showing aimspot.

		if (shouldAim)
		{	
			if (currentWeaponSetting->predEnabled)
				Aimbot::VelocityExtrapolate(player, bestSpot); // get eye pos next tick

			angle = Math::CalcAngle(localEye, bestSpot);

			if (currentWeaponSetting->errorMarginEnabled)
			{
				static int lastShotFired = 0;
				if ((localplayer->GetShotsFired() > lastShotFired)) //get new random spot when firing a shot or when aiming at a new target
					lastRandom = Aimbot::ApplyErrorToAngle(&angle, currentWeaponSetting->errorMarginValue);

				angle += lastRandom;
				lastShotFired = localplayer->GetShotsFired();
			}
		}		
		AutoShoot(localplayer, activeWeapon, cmd, currentWeaponSetting);
		Aimbot::AutoPistol(activeWeapon, cmd, currentWeaponSetting->autoPistolEnabled);
	}
	else // No player to Shoot
	{
        Settings::Debug::AutoAim::target.Zero();	
		lastRandom.Init(0.f,0.f,0.f);
		if (cmd->buttons & IN_ATTACK)	angle = oldAngle;
    }

	Legitbot::Smooth(player, angle, shouldAim, *currentWeaponSetting);
	Legitbot::AimStep(player, angle, cmd, shouldAim, *currentWeaponSetting);
	Aimbot::NoRecoil(angle, cmd, localplayer, activeWeapon, currentWeaponSetting->silent);
	Aimbot::AutoPistol(activeWeapon, cmd, currentWeaponSetting->autoPistolEnabled);
	
    Math::NormalizeAngles(angle);
    Math::ClampAngles(angle);

	Aimbot::FixMouseDeltas(cmd, angle, oldAngle);
	
	if (cmd->buttons & IN_ATTACK)
		cmd->viewangles = angle;

	if( !currentWeaponSetting->silent && CreateMove::sendPacket)
    	engine->SetViewAngles(angle);

}

void Legitbot::FireGameEvent(IGameEvent* event)
{
	if(!Settings::Legitbot::enabled || !event)
		return;

	if (strcmp(event->GetName(), XORSTR("player_connect_full")) == 0 || strcmp(event->GetName(), XORSTR("cs_game_disconnected")) == 0 )
	{
		if (event->GetInt(XORSTR("userid")) && engine->GetPlayerForUserID(event->GetInt(XORSTR("userid"))) != engine->GetLocalPlayer())
			return;
		Legitbot::friends.clear();
	}
	if( strcmp(event->GetName(), XORSTR("player_death")) == 0 )
	{
		int attacker_id = engine->GetPlayerForUserID(event->GetInt(XORSTR("attacker")));
		int deadPlayer_id = engine->GetPlayerForUserID(event->GetInt(XORSTR("userid")));

		if (attacker_id == deadPlayer_id) // suicide
			return;

		if (attacker_id != engine->GetLocalPlayer())
			return;

		killTimes.push_back(Util::GetEpochTime());
	}
}

