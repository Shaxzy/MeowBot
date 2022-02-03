#include "walkbot.h"

#include "../core/variables.h"
#include "../sdk/definitions.h"
#include "../utilities/math.h"
#include "../utilities/logging.h"
#include "../core/interfaces.h"
#include "../global.h"

#include <regex>
#include <thread>

void CWalkBot::Run(CUserCmd* pCmd, CBaseEntity* pLocal)
{
	if (!LevelCheck())
		return;

	if (DoesNeedNewPath(pLocal)) {
		Reset();

		if (!pLocal->IsAlive())
			return;

		m_pCurrentTarget = GetAreaNearEnemies(pLocal);
		nav_mesh::nav_area* pAreaClosestToPlayer = GetAreaNearPlayer(pLocal);

		L::Print(fmt::format("Trying to find path to #{} at {}.", m_pCurrentTarget->get_id(), std::string(m_pNavFile->GetNameOfPlace(m_pCurrentTarget->get_place()))));
		bool bWasPathSuccessful = true;

		try
		{
			m_vAreasPath = m_pNavFile->find_path(pAreaClosestToPlayer->get_id(), m_pCurrentTarget->get_id());
		}
		catch (const std::exception&)
		{
			bWasPathSuccessful = false;
		}

		if(bWasPathSuccessful)
			L::Print(fmt::format("Path to #{} at {} found.", m_pCurrentTarget->get_id(), std::string(m_pNavFile->GetNameOfPlace(m_pCurrentTarget->get_place()))));

		return;
	}
	else {
		UpdateEnemyBlacklist();
		CalculateDeltaAvg();

		if (C::Get<bool>(Vars.bWalkbotVisualize))
			DrawAreas(pLocal);

		if (m_tCrouchTimer.Elapsed() < 250)
			pCmd->iButtons |= IN_DUCK;

		if (m_tJumpTimer.Elapsed() < 250)
			pCmd->iButtons |= IN_JUMP;

		const Vector vOurPos = pLocal->GetOrigin();
		const Vector vOurPosNoHeight = { pLocal->GetOrigin().x, pLocal->GetOrigin().y, 0.0f };

		if (C::Get<bool>(Vars.bWalkbotAutoOptimize))
			OptimizePath(vOurPos);

		const Vector vNextPos = m_vAreasPath.back()->get_center();
		const Vector vNextPosNoHeight = { m_vAreasPath.back()->get_center().x, m_vAreasPath.back()->get_center().y, 0.0f };

		static const Vector vEyesHeight = pLocal->GetEyePosition() - vOurPos;

		QAngle vCalculatedAngles = M::CalcAngle(pLocal->GetEyePosition(), vNextPos + vEyesHeight);
		QAngle vDeltaAngles = vCalculatedAngles - pCmd->angViewPoint;

		vDeltaAngles.Normalize();

		m_vDeltaAnglesHist.pop_back();
		m_vDeltaAnglesHist.push_front(fabsf(vDeltaAngles.x + vDeltaAngles.y));

		if (C::Get<bool>(Vars.bWalkbotLookAtPoint)) {
			if (C::Get<bool>(Vars.bWalkbotHumanize))
				vDeltaAngles /= 25.0f;

			pCmd->angViewPoint += vDeltaAngles;
			I::Engine->SetViewAngles(pCmd->angViewPoint);

			//if (pCmd->iButtons |= IN_ATTACK)//TEST
			//vDeltaAngles.Normalize(); //test
											 //return;//TEST
			//return false; //TEST
		}

		CalculateFootMovement(vOurPos, vNextPos, pCmd);
		DoObstacleAction(pLocal, pCmd);

		if (m_tBackUpTimer.Elapsed() < 250)
			pCmd->flForwardMove = -420.0f;

		const float flDistToNext = (vOurPos.z - 64.0f < vNextPos.z) ? vOurPosNoHeight.DistTo(vNextPosNoHeight) : vOurPos.DistTo(vNextPos);
		if (flDistToNext < 32.0f) {
			m_tSinceLastArea.Reset();
			m_vAreasPath.pop_back();
		}
	}
}

void CWalkBot::Reset(bool bReloadNav) {
	m_pCurrentTarget = nullptr;
	m_vEnemyBlacklists.clear();
	m_tDoorOpenTimer.Reset();
	m_tSinceLastArea.Reset();
	m_vAreasPath.clear();
	if(bReloadNav)
		m_szCurrentMapName.clear();
}

bool CWalkBot::IsEnemyBlacklisted(CBaseEntity* pEnt) {
	for (const auto& blEnemy : m_vEnemyBlacklists) {
		if (pEnt == blEnemy.m_pEntity)
			return true;
	}
	return false;
}

void CWalkBot::UpdateEnemyBlacklist() {
	m_vEnemyBlacklists.erase(
		std::remove_if(m_vEnemyBlacklists.begin(), m_vEnemyBlacklists.end(),
			[](const tEnemyBlacklist& blEnemy) {
				return !blEnemy.m_pEntity->IsAlive() || blEnemy.m_tTimeSinceLast.Elapsed() > 500;
			}),
		m_vEnemyBlacklists.end()
	);
}

void CWalkBot::CalculateDeltaAvg() {
	float flTotalVal = 0.0f;
	for (float flVal : m_vDeltaAnglesHist) {
		flTotalVal += flVal;
	}

	m_flDeltaAvg = flTotalVal / 30.0f;
}

void CWalkBot::DrawAreas(CBaseEntity* pLocal) {
	I::DebugOverlay->ClearAllOverlays();

	Vector vLastPoint = pLocal->GetOrigin();
	for (int i = m_vAreasPath.size() - 1; i >= 0; i--) {
		if (i == m_vAreasPath.size() - 1) {
			I::DebugOverlay->AddBoxOverlay(m_vAreasPath[i]->get_center(), Vector(-4.0f, -4.0f, 0.0f), Vector(4.0f, 4.0f, 48.0f), QAngle(), 0, 255, 0, 32, 0.16f);
			I::DebugOverlay->AddLineOverlay(vLastPoint + Vector(0.f, 0.f, 12.f), m_vAreasPath[i]->get_center() + Vector(0.f, 0.f, 12.f), 0, 255, 0, 255, 0.25f, 0.16f);
		}
		else {
			I::DebugOverlay->AddBoxOverlay(m_vAreasPath[i]->get_center(), Vector(-4.0f, -4.0f, 0.0f), Vector(4.0f, 4.0f, 48.0f), QAngle(), 255, 0, 0, 32, 0.16f);
			I::DebugOverlay->AddLineOverlay(vLastPoint + Vector(0.f, 0.f, 12.f), m_vAreasPath[i]->get_center() + Vector(0.f, 0.f, 12.f), 255, 0, 0, 255, 0.25f, 0.16f);
		}

		vLastPoint = m_vAreasPath[i]->get_center();
	}
}

#define PLAYER_BODY_RADIUS 8.0f // 16.0f

bool CWalkBot::LevelCheck() {
	std::string szMapName{ I::Engine->GetLevelName() };
	if (szMapName != m_szCurrentMapName) {
		m_szCurrentMapName = szMapName;

		std::string szFileName = m_szCurrentMapName.substr(0, m_szCurrentMapName.length() - 3).append("nav");

		static char szPathTemp[MAX_PATH];
		static int iUnused = GetModuleFileNameA(GetModuleHandle("client.dll"), szPathTemp, MAX_PATH);

		std::string szNavPath{ std::string(szPathTemp).substr(0, std::string(szPathTemp).length() - 14).append(szFileName).c_str() };

		if (std::filesystem::exists(szNavPath)) {
			if (m_pNavFile)
				delete m_pNavFile;

			L::Print(fmt::format(XorStr("Loaded .nav file for map \"{}\" from \"{}\"."), std::string(I::Engine->GetLevelNameShort()), szNavPath));

			m_pNavFile = new nav_mesh::nav_file{ szNavPath };
			return true;
		}
		else {
			L::Print(fmt::format(XorStr("Could not find .nav file for map \"{}\". Trying to save from .bsp file."), std::string(I::Engine->GetLevelNameShort())));
			I::Engine->ExecuteClientCmd("nav_save");

			int iRetryAttempts = 0;
			while (!std::filesystem::exists(szNavPath) && iRetryAttempts < 5) {
				iRetryAttempts++;
				std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
			}

			if (std::filesystem::exists(szNavPath)) {
				if (m_pNavFile)
					delete m_pNavFile;

				m_pNavFile = new nav_mesh::nav_file();
				m_pNavFile->load(szNavPath);
				return true;
			}
			else {
				L::Print(fmt::format(XorStr("Unable to save .nav file for map \"{}\". These files cannot be generated while on official Valve servers."), std::string(I::Engine->GetLevelNameShort())));
				C::Get<bool>(Vars.bWalkbot) = false;
				Reset(true);
				return false;
			}
		}
	}
	return true;
}

#define MAKE_TRACE(varname, vecStart, anglesin, mask) Trace_t varname{}; \
{\
	Vector vecEnd, vecForward;\
	M::AngleVectors(anglesin, &vecForward);\
	vecEnd = vecStart + vecForward * PLAYER_BODY_RADIUS + vecForward * 32.f;\
	Ray_t ray(vecStart, vecEnd);\
	CTraceFilter filter(pLocal);\
	I::EngineTrace->TraceRay(ray, mask, &filter, &varname);\
	I::DebugOverlay->AddLineOverlayAlpha(vecStart, vecEnd, 255, 255, 255, 255, true, 1.0f);\
}

void CWalkBot::DoObstacleAction(CBaseEntity* pLocal, CUserCmd* pCmd) {
	MAKE_TRACE(step_front, pLocal->GetOrigin() + Vector(0.0f, 0.0f, 15.0f), QAngle(0.0f, pCmd->angViewPoint.y, 0.0f), MASK_PLAYERSOLID);
	MAKE_TRACE(crouchjump_front, pLocal->GetOrigin() + Vector(0.0f, 0.0f, 54.0f), QAngle(0.0f, pCmd->angViewPoint.y, 0.0f), MASK_PLAYERSOLID);
	MAKE_TRACE(object_left, pLocal->GetOrigin() + Vector(0.0f, 0.0f, 32.0f), QAngle(0.0f, pCmd->angViewPoint.y - 45.0f, 0.0f).Normalize(), MASK_PLAYERSOLID);
	MAKE_TRACE(object_right, pLocal->GetOrigin() + Vector(0.0f, 0.0f, 32.0f), QAngle(0.0f, pCmd->angViewPoint.y + 45.0f, 0.0f).Normalize(), MASK_PLAYERSOLID);
	MAKE_TRACE(windowtest_front, pLocal->GetOrigin() + Vector(0.0f, 0.0f, pLocal->GetEyePosition().z - pLocal->GetOrigin().z), QAngle(0.0f, pCmd->angViewPoint.y, 0.0f), CONTENTS_WINDOW);
	MAKE_TRACE(standing_front, pLocal->GetOrigin() + Vector(0.0f, 0.0f, 72.0f), QAngle(0.0f, pCmd->angViewPoint.y, 0.0f), MASK_PLAYERSOLID);

	{
		if (!(standing_front.DidHit() || standing_front.pHitEntity != nullptr)) {
			if (step_front.DidHit() || step_front.pHitEntity != nullptr) {
				if (m_tJumpTimer.Elapsed() > 250) {
					if (crouchjump_front.DidHit() || crouchjump_front.pHitEntity != nullptr) {
						if (m_tCrouchTimer.Elapsed() > 250) {
							m_tCrouchTimer.Reset();
						}
					}
					m_tJumpTimer.Reset();
				}
			}
		}
	}

	{
		if (standing_front.DidHit() || standing_front.pHitEntity != nullptr) {
			if (step_front.DidHit() || step_front.pHitEntity != nullptr) {
				if (m_tBackUpTimer.Elapsed() > 250) {
					m_tBackUpTimer.Reset();
				}
			}
		}
	}

	{
		if (windowtest_front.DidHit()) {
			pCmd->iButtons |= IN_ATTACK;
		}
	}

	{
		bool bDidHitLeft = object_left.DidHit() || object_left.pHitEntity != nullptr;
		bool bDidHitRight = object_right.DidHit() || object_right.pHitEntity != nullptr;
		if (bDidHitLeft && !bDidHitRight) {
			pCmd->flSideMove = -420.0f;
		}
		else if(bDidHitRight && !bDidHitLeft) {
			pCmd->flSideMove = 420.0f;
		}
	}

	{
		if (standing_front.pHitEntity != nullptr) {
			const std::string szEntName{ standing_front.pHitEntity->GetClientClass()->szNetworkName };
			if (szEntName == "CPropDoorRotating" &&	m_tDoorOpenTimer.Elapsed() > 500) {
				m_tDoorOpenTimer.Reset();
				pCmd->iButtons |= IN_USE;
			}
		}
	}

	{
		if (standing_front.pHitEntity != nullptr || standing_front.DidHit()) {
			if (!(step_front.pHitEntity != nullptr || standing_front.DidHit())) {
				const std::string szEntName{ standing_front.pHitEntity->GetClientClass()->szNetworkName };
				if (szEntName != "CPropDoorRotating") {
					pCmd->iButtons |= IN_DUCK;
				}
			}
		}
	}
}

void CWalkBot::OptimizePath(Vector vPlayerPos) {
	if (m_vAreasPath.size() > 1) {
		const float flDistToFirst = vPlayerPos.DistTo(m_vAreasPath.back()->get_center());
		const float flDistToSecond = vPlayerPos.DistTo(m_vAreasPath[m_vAreasPath.size() - 2]->get_center());

		if (flDistToFirst > flDistToSecond) {
			m_vAreasPath.pop_back();
			m_tSinceLastArea.Reset();
		}
	}
}

void CWalkBot::CalculateFootMovement(Vector vPlayerPos, Vector vPointPos, CUserCmd* pCmd) {
	Vector vDelta = vPointPos - vPlayerPos;

	if (vDelta.Length() == 0.0f) {
		pCmd->flForwardMove = 0.0f;
		pCmd->flSideMove = 0.0f;
		return;
	}

	float speed = sqrt(vDelta.x * vDelta.x + vDelta.y * vDelta.y);
	QAngle vAngles;
	M::VectorAngles(vDelta, vAngles);
	float flYaw = M_DEG2RAD(vAngles.y - pCmd->angViewPoint.y);
	pCmd->flForwardMove = cos(flYaw) * 450.0f;
	pCmd->flSideMove = -sin(flYaw) * 450.0f;
	return;
}

bool CWalkBot::DoesNeedNewPath(CBaseEntity* pLocal) {
	if (m_vAreasPath.size() == 0) {
		return true;
	}

	Vector flNextAreaCenter = m_vAreasPath.back()->get_center();
	if (flNextAreaCenter.z > pLocal->GetEyePosition().z + 24.0f) {
		return true;
	}

	if (flNextAreaCenter.DistTo(pLocal->GetOrigin()) > 1024.0f) {
		return true;
	}

	if (!pLocal->IsAlive()) {
		return true;
	}

	if (m_tSinceLastArea.Elapsed() > 7000) {
		return true;
	}

	return false;
}

nav_mesh::nav_area* CWalkBot::GetAreaNearEnemies(CBaseEntity* pLocal) {
	nav_mesh::nav_area* pCurrentTarget = nullptr;

	CBaseEntity* pFurthestPlayer = nullptr;
	float flBestDistance = 0.0f;
	for (int i = 1; i < I::Globals->nMaxClients; i++)
	{
		CBaseEntity* entity = reinterpret_cast<CBaseEntity*>(I::ClientEntityList->GetClientEntity(i));
		if (!entity
			|| entity == G::pLocal
			|| entity->IsDormant()
			|| entity->GetLifeState() != LIFE_ALIVE
			|| entity->GetClientClass()->nClassID != EClassIndex::CCSPlayer
			|| entity->GetTeam() == G::pLocal->GetTeam()
			|| IsEnemyBlacklisted(entity))
			continue;

		if (entity->GetAbsOrigin().DistTo(G::pLocal->GetAbsOrigin()) > flBestDistance) {
			flBestDistance = entity->GetAbsOrigin().DistTo(G::pLocal->GetAbsOrigin());
			pFurthestPlayer = entity;
		}
	}

	if (pFurthestPlayer) {
		nav_mesh::nav_area* pNavArea = m_pNavFile->GetNearestArea(pFurthestPlayer->GetOrigin());
		if (pNavArea) {
			pCurrentTarget = pNavArea;
			m_vEnemyBlacklists.push_back(tEnemyBlacklist(pFurthestPlayer));
		}
	}

	if (!pCurrentTarget) {
		int iRandomIdx = M::RandomInt(0, m_pNavFile->m_areas.size() - 1);
		pCurrentTarget = &(m_pNavFile->m_areas[iRandomIdx]);
	}


	return pCurrentTarget;
}

nav_mesh::nav_area* CWalkBot::GetAreaNearPlayer(CBaseEntity* pLocal) {
	const Vector vPlayerOrigin = pLocal->GetOrigin();
	nav_mesh::nav_area* pNearestArea = nullptr;
	float flBestDistance = FLT_MAX;

	for (size_t i = 0; i < m_pNavFile->m_areas.size(); i++) {
		const Vector vCenter = m_pNavFile->m_areas[i].get_center();
		const float flDistance = vCenter.DistTo(vPlayerOrigin);
		if (flDistance < flBestDistance) {
			flBestDistance = flDistance;
			pNearestArea = &(m_pNavFile->m_areas[i]);
		}
	}

	return pNearestArea;
}