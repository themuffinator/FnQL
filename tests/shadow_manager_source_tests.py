from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in text:
        failures.append(f"missing {label}: {needle}")


def require_order(text: str, needles: list[str], label: str, failures: list[str]) -> None:
    position = 0
    for needle in needles:
        found = text.find(needle, position)
        if found < 0:
            failures.append(f"missing ordered {label}: {needle}")
            return
        position = found + len(needle)


def section(text: str, start: str, end: str, label: str, failures: list[str]) -> str:
    start_index = text.find(start)
    if start_index < 0:
        failures.append(f"missing section start {label}: {start}")
        return ""
    end_index = text.find(end, start_index)
    if end_index < 0:
        failures.append(f"missing section end {label}: {end}")
        return text[start_index:]
    return text[start_index:end_index]


def check_source(label: str, root: str, failures: list[str]) -> None:
    header = (ROOT / root / "tr_local.h").read_text(encoding="utf-8")
    main = (ROOT / root / "tr_main.c").read_text(encoding="utf-8")
    backend = (ROOT / root / "tr_backend.c").read_text(encoding="utf-8")
    cmds = (ROOT / root / "tr_cmds.c").read_text(encoding="utf-8")

    summary = section(
        main,
        "static void R_UpdateShadowManagerSummary",
        "static void R_PlanFrameShadows",
        f"{label} shadow manager summary",
        failures,
    )
    store_point = section(
        main,
        "static void R_ShadowManagerStorePointLightRecord",
        "static void R_ShadowManagerAddPointCandidate",
        f"{label} point candidate storage",
        failures,
    )
    assign_point = section(
        main,
        "static void R_ShadowManagerAssignPointAtlas",
        "static qboolean R_ShadowManagerPlanPointAtlas",
        f"{label} point atlas assignment",
        failures,
    )
    plan_point = section(
        main,
        "static void R_PlanDlightShadows",
        "#endif // USE_PMLIGHT",
        f"{label} point shadow planner",
        failures,
    )

    require(header, "SHADOW_MANAGER_PASS_POINT_ATLAS = 1 << 0", f"{label} point pass bit", failures)
    require(header, "SHADOW_MANAGER_PASS_SPOT_ATLAS = 1 << 1", f"{label} spot pass bit", failures)
    require(header, "SHADOW_MANAGER_PASS_CSM_ATLAS = 1 << 2", f"{label} CSM atlas pass bit", failures)
    require(header, "SHADOW_MANAGER_PASS_CSM_RECEIVER = 1 << 3", f"{label} CSM receiver pass bit", failures)
    require(header, "#define SHADOW_MANAGER_MAX_SCHEDULED_PASSES 4", f"{label} schedule order cap", failures)
    require(header, "unsigned int\tscheduledPassMask;", f"{label} schedule mask field", failures)
    require(header, "shadowManagerPass_t scheduledPassOrder[SHADOW_MANAGER_MAX_SCHEDULED_PASSES];", f"{label} schedule order field", failures)
    require(header, "csmPlan_t\tcsmPlan;", f"{label} manager-owned CSM plan field", failures)
    require(header, "shadowAtlasPublication_t pointAtlasPublication;", f"{label} point publication record", failures)
    require(header, "shadowAtlasPublication_t csmAtlasPublication;", f"{label} CSM publication record", failures)
    require(header, "shadowAtlasPublication_t spotAtlasPublication;", f"{label} spot publication record", failures)
    require(header, "int\t\tc_csmSkippedZeroCascades;", f"{label} CSM zero-cascade skip counter", failures)
    require(header, "extern cvar_t\t*r_csmDebugFallback;", f"{label} CSM debug fallback cvar", failures)
    require(header, "int\t\t\tspotStaticCandidateCount;", f"{label} static spot candidate counter", failures)
    require(header, "int\t\t\tspotStaticPlanCount;", f"{label} static spot plan counter", failures)
    require(header, "int\t\t\tspotSurfaceCandidateCount;", f"{label} surfacelight spot candidate counter", failures)
    require(header, "int\t\t\tspotSurfacePlanCount;", f"{label} surfacelight spot plan counter", failures)
    require(header, "int\t\t\tspotSurfaceRejectedWeak;", f"{label} surfacelight weak rejection counter", failures)
    require(header, "int\t\t\tspotSurfaceRejectedOffView;", f"{label} surfacelight off-view rejection counter", failures)
    require(header, "int\t\t\tspotSurfaceRejectedOverBudget;", f"{label} surfacelight budget rejection counter", failures)
    require(header, "int\t\t\tspotSurfaceRejectedMalformed;", f"{label} surfacelight malformed rejection counter", failures)
    require(header, "int\t\t\tspotSurfaceCandidateTileMin;", f"{label} surfacelight candidate tile range", failures)
    require(header, "int\t\t\tspotSurfaceFootprintMin;", f"{label} surfacelight footprint telemetry range", failures)
    require(header, "int\t\t\tspotSurfaceCasterRadiusMin;", f"{label} surfacelight caster radius telemetry range", failures)
    require(header, "int\t\t\tspotSurfacePlanTileMin;", f"{label} surfacelight effective tile range", failures)
    require(header, "int\t\t\tspotSurfacePlanAllocatedCount;", f"{label} surfacelight atlas allocation counter", failures)
    require(header, "int\t\t\tspotSurfaceConeOuterMax;", f"{label} surfacelight cone telemetry range", failures)
    require(header, "static ID_INLINE qboolean R_ShadowManagerPassScheduled", f"{label} schedule helper", failures)
    require(header, "manager->scheduledPassMask & (unsigned int)pass", f"{label} schedule helper mask test", failures)
    require(header, "static ID_INLINE void R_ShadowManagerPublishPointAtlas", f"{label} point publication helper", failures)
    require(header, "static ID_INLINE void R_ShadowManagerPublishCSMAtlas", f"{label} CSM publication helper", failures)
    require(header, "static ID_INLINE void R_ShadowManagerPublishSpotAtlas", f"{label} spot publication helper", failures)
    require(header, "static ID_INLINE int R_ShadowFilterPadTexels", f"{label} atlas filter pad helper", failures)
    require(header, "static ID_INLINE int R_ShadowAtlasClampTexels", f"{label} atlas clamp helper", failures)
    require(header, "manager->pointAtlasPublished = manager->pointAtlasPublication.published;", f"{label} point publication mirror", failures)
    require(header, "manager->csmAtlasPublished = manager->csmAtlasPublication.published;", f"{label} CSM publication mirror", failures)
    require(header, "manager->spotAtlasPublished = manager->spotAtlasPublication.published;", f"{label} spot publication mirror", failures)
    require(header, "static ID_INLINE const csmPlan_t *R_ShadowManagerCSMPlan", f"{label} manager CSM plan helper", failures)
    require(header, "manager->planned && manager->csmPlan.enabled", f"{label} manager CSM plan helper gate", failures)
    require(main, "static void R_ShadowManagerSchedulePass", f"{label} schedule setter", failures)
    require(main, "manager->scheduledPassMask |= (unsigned int)pass;", f"{label} schedule setter mask write", failures)
    require(main, "manager->scheduledPassOrder[manager->scheduledPasses] = pass;", f"{label} schedule setter order write", failures)
    require(main, "R_ShadowManagerPublishCSMAtlas( manager, qfalse, 0 );", f"{label} CSM publication initialization", failures)
    require(main, "debugFallback = r_csmDebugFallback ? r_csmDebugFallback->integer : 0;", f"{label} CSM debug fallback selector", failures)
    require(main, "tr.pc.c_csmSkippedZeroCascades++;", f"{label} CSM zero-cascade fallback accounting", failures)
    require(main, "( r_csmDebugFallback && r_csmDebugFallback->integer == 1 )", f"{label} manager no-world debug fallback marker", failures)
    require(main, "R_ShadowManagerPublishPointAtlas( manager, qfalse, 0 );", f"{label} point publication initialization", failures)
    require(main, "R_ShadowManagerPublishSpotAtlas( manager, qfalse, 0 );", f"{label} spot publication initialization", failures)
    require(main, "static qboolean R_ShadowPointCandidateBetter", f"{label} deterministic point atlas tie-break helper", failures)
    require(main, "candidate->dlightIndex < best->dlightIndex", f"{label} point atlas dlight-index tie-break", failures)
    require(main, "static qboolean R_ShadowSpotCandidateBetter", f"{label} deterministic spot atlas tie-break helper", failures)
    require(main, "candidate->sourceIndex < best->sourceIndex", f"{label} spot atlas source-index tie-break", failures)
    require(main, "manager->spotStaticCandidateCount++;", f"{label} static spot candidate accounting", failures)
    require(main, "manager->spotSurfaceCandidateCount++;", f"{label} surfacelight spot candidate accounting", failures)
    require(main, "manager->spotStaticPlanCount++;", f"{label} static spot plan accounting", failures)
    require(main, "manager->spotSurfacePlanCount++;", f"{label} surfacelight spot plan accounting", failures)
    require(main, "static void R_ShadowSpotTelemetryRange", f"{label} surfacelight spot telemetry range helper", failures)
    require(main, "static qboolean R_ShadowSpotSurfaceMalformed", f"{label} surfacelight malformed helper", failures)
    require(main, "proxy->shadowCasterRadius <= 0.0f || proxy->shadowConeAngle <= 0.0f", f"{label} surfacelight malformed caster/cone gate", failures)
    require(main, "R_ShadowManagerRecordSurfaceSpotCandidateTelemetry( record );", f"{label} surfacelight candidate telemetry call", failures)
    require(main, "R_ShadowManagerRecordSurfaceSpotPlanTelemetry( plan );", f"{label} surfacelight plan telemetry call", failures)
    require(main, "manager->spotSurfaceFootprintMin", f"{label} surfacelight footprint telemetry accounting", failures)
    require(main, "manager->spotSurfaceCasterRadiusMin", f"{label} surfacelight caster telemetry accounting", failures)
    require(main, "proxy->shadowCasterRadius : proxy->radius", f"{label} surfacelight caster radius fallback", failures)
    require(main, "proxy->shadowConeAngle : SPOT_SHADOW_SURFACELIGHT_OUTER_ANGLE", f"{label} surfacelight cone fallback", failures)
    require(main, "proxy->origin, proxy->normal, proxy->color, casterRadius, proxy->intensity", f"{label} surfacelight spot candidate uses caster radius", failures)
    require(main, "tr.shadowManager.spotSurfaceRejectedMalformed++;", f"{label} surfacelight malformed rejection accounting", failures)
    require(main, "tr.shadowManager.spotSurfaceRejectedWeak++;", f"{label} surfacelight weak rejection accounting", failures)
    require(main, "tr.shadowManager.spotSurfaceRejectedOffView++;", f"{label} surfacelight off-view rejection accounting", failures)
    require(main, "tr.shadowManager.spotSurfaceRejectedOverBudget =", f"{label} surfacelight budget rejection accounting", failures)
    require(store_point, "record->shadowIndex = -1;", f"{label} point candidate has no dlight atlas mirror", failures)
    require(store_point, "record->atlasBaseFace = -1;", f"{label} point candidate atlas base defaults to manager-owned allocation", failures)
    require(store_point, "record->atlasAllocated = qfalse;", f"{label} point candidate starts unallocated", failures)
    require(assign_point, "record->atlasAllocated = qfalse;", f"{label} manager point assignment clears allocation before layout", failures)
    require(assign_point, "record->atlasAllocated = qtrue;", f"{label} manager point assignment owns allocation", failures)
    require(plan_point, "R_ShadowManagerAssignPointAtlas( plan, planned,", f"{label} planner assigns manager point atlas", failures)
    if "R_ShadowManagerApplyPointPlanToDlight" in main:
        failures.append(f"{label} point plans still sync atlas assignment back to dlight_t")
    if "dl->shadowPlanned = qtrue;" in plan_point:
        failures.append(f"{label} point planner still marks dlight_t shadowPlanned in planned manager path")
    require_order(
        summary,
        [
            "manager->scheduledPasses = 0;",
            "manager->scheduledPassMask = 0;",
            "R_ShadowManagerSchedulePass( manager, SHADOW_MANAGER_PASS_CSM_ATLAS,",
            "R_ShadowManagerSchedulePass( manager, SHADOW_MANAGER_PASS_CSM_RECEIVER,",
            "R_ShadowManagerSchedulePass( manager, SHADOW_MANAGER_PASS_POINT_ATLAS,",
            "R_ShadowManagerSchedulePass( manager, SHADOW_MANAGER_PASS_SPOT_ATLAS,",
        ],
        f"{label} schedule mask construction",
        failures,
    )
    require_order(
        summary,
        [
            "manager->csmPlanned = ( tr.csm.enabled && tr.csm.cascadeCount > 0 ) ? qtrue : qfalse;",
            "manager->csmCascadeCount = tr.csm.cascadeCount;",
            "manager->csmResolution = tr.csm.resolution;",
            "manager->csmPlan = tr.csm;",
        ],
        f"{label} manager CSM plan ownership copy",
        failures,
    )
    require(cmds, "cmd->csm = tr.csm;", f"{label} compatibility CSM command mirror", failures)
    require(cmds, "cmd->shadowManager = tr.shadowManager;", f"{label} manager command copy", failures)
    require_order(
        backend,
        [
            "tr.shadowManager = cmd->shadowManager;",
            "managerCSM = R_ShadowManagerCSMPlan( &tr.shadowManager );",
            "tr.csm = managerCSM ? *managerCSM : cmd->csm;",
        ],
        f"{label} backend prefers manager-owned CSM plan",
        failures,
    )
    require(backend, "R_ShadowManagerPassScheduled( &tr.shadowManager,\n\t\t\tSHADOW_MANAGER_PASS_POINT_ATLAS )", f"{label} point atlas schedule gate", failures)
    require(backend, "R_ShadowManagerPassScheduled( &tr.shadowManager,\n\t\t\t\tSHADOW_MANAGER_PASS_CSM_ATLAS )", f"{label} CSM atlas schedule gate", failures)
    require(backend, "R_ShadowManagerPassScheduled( &tr.shadowManager,\n\t\t\t\t\tSHADOW_MANAGER_PASS_CSM_RECEIVER )", f"{label} CSM receiver schedule gate", failures)
    require(backend, "R_ShadowManagerPassScheduled( &tr.shadowManager,\n\t\t\tSHADOW_MANAGER_PASS_SPOT_ATLAS )", f"{label} spot atlas schedule gate", failures)
    require(backend, "static void RB_RunScheduledShadowManagerPreMainPasses", f"{label} scheduled pre-main executor", failures)
    require(backend, "passOrder = tr.shadowManager.scheduledPassOrder;", f"{label} backend iterates manager pass order", failures)
    require(backend, "RB_RunScheduledShadowManagerPass( passOrder[i], drawSurfs, numDrawSurfs );", f"{label} backend dispatches ordered passes", failures)
    require(backend, "RB_RunScheduledShadowManagerPass( SHADOW_MANAGER_PASS_CSM_RECEIVER,", f"{label} CSM receiver dispatches through schedule", failures)
    require(backend, "RB_RunScheduledShadowManagerPreMainPasses( cmd->drawSurfs, cmd->numDrawSurfs );", f"{label} drawsurfs uses scheduled pre-main executor", failures)
    require(backend, "R_ShadowManagerPublishPointAtlas( &tr.shadowManager,", f"{label} point atlas backend publication", failures)
    require(backend, "R_ShadowManagerPublishCSMAtlas( &tr.shadowManager,", f"{label} CSM atlas backend publication", failures)
    require(backend, "R_ShadowManagerPublishSpotAtlas( &tr.shadowManager,", f"{label} spot atlas backend publication", failures)
    require(backend, "!tr.shadowManager.csmAtlasPublication.published", f"{label} CSM receiver uses publication record", failures)
    require(cmds, "sched:%i mask:%x", f"{label} schedule mask debug label", failures)
    require(cmds, "manager->scheduledPasses, manager->scheduledPassMask", f"{label} schedule mask debug arg", failures)
    require(cmds, "manager->pointAtlasPublication.published", f"{label} point publication debug", failures)
    require(cmds, "manager->spotAtlasPublication.published", f"{label} spot publication debug", failures)
    require(cmds, "manager->csmAtlasPublication.published", f"{label} CSM publication debug", failures)
    require(cmds, "static void R_PrintShadowAtlasContractDebug", f"{label} atlas contract debug helper", failures)
    require(cmds, "\"shadow atlas contract backend:%s atlas:%s active:%i", f"{label} atlas contract debug label", failures)
    require(cmds, "sampler:clamp-edge allocation:%s deterministic:1", f"{label} atlas sampler/allocation contract", failures)
    require(cmds, "R_PrintShadowAtlasContractDebug( manager, &manager->csmPlan,", f"{label} atlas contract debug call", failures)
    require(cmds, "\"csm plan cascades:0 skip no-world\\n\"", f"{label} CSM no-world fallback debug", failures)
    require(cmds, "\"csm plan cascades:0 skip zero-cascade\\n\"", f"{label} CSM zero-cascade fallback debug", failures)
    require(cmds, "src static:%i/%i surface:%i/%i", f"{label} spot source debug label", failures)
    require(cmds, "manager->spotStaticPlanCount, manager->spotStaticCandidateCount", f"{label} static spot debug args", failures)
    require(cmds, "manager->spotSurfacePlanCount, manager->spotSurfaceCandidateCount", f"{label} surfacelight spot debug args", failures)
    require(cmds, "surfacelight spot plan cand:%i plan:%i req:%i-%i foot:%i-%i caster:%i-%i", f"{label} surfacelight spot telemetry label", failures)
    require(cmds, "manager->spotSurfaceFootprintMin, manager->spotSurfaceFootprintMax", f"{label} surfacelight footprint debug args", failures)
    require(cmds, "manager->spotSurfaceCasterRadiusMin, manager->spotSurfaceCasterRadiusMax", f"{label} surfacelight caster radius debug args", failures)
    require(cmds, "manager->spotSurfacePlanAllocatedCount", f"{label} surfacelight atlas allocation debug arg", failures)
    require(cmds, "manager->spotSurfaceRejectedWeak", f"{label} surfacelight weak rejection debug arg", failures)
    require(cmds, "manager->spotSurfaceRejectedMalformed", f"{label} surfacelight malformed rejection debug arg", failures)


def main() -> int:
    failures: list[str] = []
    check_source("OpenGL", "code/renderer", failures)
    check_source("Vulkan", "code/renderervk", failures)

    if failures:
        print("Shadow manager source contract violations:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("Checked shadow manager source contract.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
