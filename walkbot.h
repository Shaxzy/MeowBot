#pragma once
#include "../common.h"
#include "../sdk/datatypes/usercmd.h"
#include "../sdk/entity.h"
#include "../utilities.h"
#include "../sdk/nav/nav_area.h"
#include "../utilities/micropather.h"
#include "../sdk/nav/nav_file.h"
#include "../utilities/micropather.h"

struct tEnemyBlacklist {
	tEnemyBlacklist(CBaseEntity* pEnt) {
		m_pEntity = pEnt;
		m_tTimeSinceLast.Reset();
	};

	bool ShouldRemove() {
		return m_tTimeSinceLast.Elapsed() > 5000;
	}

	CBaseEntity* m_pEntity;
	CTimer m_tTimeSinceLast{};
};

class CWalkBot : public CSingleton<CWalkBot>
{
public:
	void Run(CUserCmd* pCmd, CBaseEntity* pLocal);
	void Reset(bool bReloadNav = false);
	inline float GetAvgViewDelta() {
		return m_flDeltaAvg;
	};

private:
	float m_flDeltaAvg;
	CTimer m_tDoorOpenTimer{};
	CTimer m_tSinceLastArea{};
	CTimer m_tCrouchTimer{};
	CTimer m_tBackUpTimer{};
	CTimer m_tJumpTimer{};
	std::string m_szCurrentMapName{};
	nav_mesh::nav_file* m_pNavFile = nullptr;
	nav_mesh::nav_area* m_pCurrentTarget{};
	std::vector<nav_mesh::nav_area*> m_vAreasPath{};
	std::deque<float> m_vDeltaAnglesHist{ 30, 999.0f };
	std::deque<tEnemyBlacklist> m_vEnemyBlacklists{};

	void UpdateEnemyBlacklist();
	bool IsEnemyBlacklisted(CBaseEntity* pEnt);
	void CalculateDeltaAvg();
	void DrawAreas(CBaseEntity* pLocal);
	bool LevelCheck();
	void DoObstacleAction(CBaseEntity* pLocal, CUserCmd* pCmd);
	void OptimizePath(Vector vPlayerPos);
	void CalculateFootMovement(Vector vPlayerPos, Vector vPointPos, CUserCmd* pCmd);
	bool DoesNeedNewPath(CBaseEntity* pLocal);
	nav_mesh::nav_area* GetAreaNearEnemies(CBaseEntity* pLocal);
	nav_mesh::nav_area* GetAreaNearPlayer(CBaseEntity* pLocal);
};
