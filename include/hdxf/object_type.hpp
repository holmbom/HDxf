#pragma once

// The object types.

#include "core.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// Objects (OBJECTS section, R13+)
// ---------------------------------------------------------------------------
//
// One Object per record in the OBJECTS section, in file order. The object types HDxf
// interprets have typed data; any other is UnknownObjectData with its groups. Handles
// that point to other records are kept as the hex strings of the file.

enum class ObjectType {
    Dictionary,             // DICTIONARY
    DictionaryWithDefault,  // ACDBDICTIONARYWDFLT
    XRecord,                // XRECORD
    Group,                  // GROUP
    Layout,                 // LAYOUT
    PlotSettings,           // PLOTSETTINGS
    DictionaryVar,          // DICTIONARYVAR
    Placeholder,            // ACDBPLACEHOLDER
    MlineStyle,             // MLINESTYLE
    ImageDef,               // IMAGEDEF
    ImageDefReactor,        // IMAGEDEF_REACTOR
    RasterVariables,        // RASTERVARIABLES
    DbColor,                // DBCOLOR (a color book color, in ACAD_COLOR)
    MLeaderStyle,           // MLEADERSTYLE (in ACAD_MLEADERSTYLE)
    GeoData,                // GEODATA (the model space's ACAD_GEOGRAPHICDATA)
    WipeoutVariables,       // WIPEOUTVARIABLES (ACAD_WIPEOUT_VARS)
    Scale,                  // SCALE (an annotation scale, in ACAD_SCALELIST)
    Sun,                    // SUN (a viewport's or the model space's sun)
    DetailViewStyle,        // ACDBDETAILVIEWSTYLE (in ACAD_DETAILVIEWSTYLE; plan phase 6, B77)
    SectionViewStyle,       // ACDBSECTIONVIEWSTYLE (in ACAD_SECTIONVIEWSTYLE)
    NavisworksModelDef,     // NAVISWORKSMODELDEF (in ACAD_BIM_DEFINITIONS; 2013+, B78)
    // --- 9.9 evaluation graph and dynamic block bases ---
    EvaluationGraph,        // ACAD_EVALUATION_GRAPH (in ACAD_ENHANCEDBLOCK; EvaluationGraphData)
    BlockGripLocation,      // BLOCKGRIPLOCATIONCOMPONENT (owned by the graph; BlockGripLocationData)
    BlockLinearParameter,   // BLOCKLINEARPARAMETER (BlockLinearParameterData)
    BlockLinearGrip,        // BLOCKLINEARGRIP (BlockLinearGripData)
    BlockVisibilityParameter, // BLOCKVISIBILITYPARAMETER (BlockVisibilityParameterData)
    BlockVisibilityGrip,    // BLOCKVISIBILITYGRIP (BlockVisibilityGripData)
    BlockScaleAction,       // BLOCKSCALEACTION (BlockScaleActionData)
    BlockRepresentation,    // ACDB_BLOCKREPRESENTATION_DATA (in an INSERT's AcDbBlockRepresentation; BlockRepresentationData)
    DynamicBlockPurgePreventer, // ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION (DynamicBlockPurgePreventerData)
    // --- 9.6 underlay definitions ---
    PdfDefinition,          // PDFDEFINITION (in ACAD_PDFDEFINITIONS; 2000+): UnderlayDefinitionData
    DwfDefinition,          // DWFDEFINITION (in ACAD_DWFDEFINITIONS)
    DgnDefinition,          // DGNDEFINITION (in ACAD_DGNDEFINITIONS)
    RapidRtRenderSettings, // RAPIDRTRENDERSETTINGS (a preset, in ACAD_RENDER_RAPIDRT_SETTINGS; 2013+)
    RapidRtRenderEnvironment, // RAPIDRTRENDERENVIRONMENT (the IBL environment, ACAD_BACKGROUND; 2013+)
    // Draw order, clipping and indexes (phase 9, step 9.4)
    SortentsTable,          // SORTENTSTABLE (a space's draw order, in the block record's ACAD_SORTENTS)
    SpatialFilter,          // SPATIAL_FILTER (an INSERT's or xref's clip, in its ACAD_FILTER/SPATIAL)
    LayerIndex,             // LAYER_INDEX (in a space's ACAD_INDEX)
    SpatialIndex,           // SPATIAL_INDEX (in a space's ACAD_INDEX)
    IdBuffer,               // IDBUFFER (a list of objects: a layer index's members)
    LayerFilter,            // LAYER_FILTER (an INSERT's or xref's layer filter, in its ACAD_FILTER/LAYER)
    // --- 9.3 OLE and proxy classes ---
    ProxyObject,            // ACAD_PROXY_OBJECT (a custom object whose application is not loaded): ProxyObjectData
    TableStyle,             // TABLESTYLE (in ACAD_TABLESTYLE; phase 9, step 9.1)
    CellStyleMap,           // CELLSTYLEMAP (in a TABLESTYLE's extension dictionary)
    TableContent,           // TABLECONTENT (a table's round-trip content)
    TableGeometry,          // TABLEGEOMETRY (a table's round-trip cell sizes)
    // --- 9.8 fields and document data ---
    Field,                  // FIELD (a text's, a table cell's or an attribute's field; AcDbField)
    FieldList,              // FIELDLIST (the drawing's fields, in ACAD_FIELDLIST)
    DataTable,              // DATATABLE (AcDbDataTable; 2004 and later: a table's pre-2007 round-trip cells)
    LegacyDataTable,        // ACDBDATATABLE (the same data, as DXF 2000 names the record)
    ObjectPtr,              // OBJECT_PTR (an AutoCAD SQL Extension link's object: the base record and its DC015 XDATA)
    VbaProject,             // VBA_PROJECT (the drawing's embedded VBA project: AcDbVbaProject)
    // --- 9.7 materials and visual styles ---
    Material,               // MATERIAL (in ACAD_MATERIAL; MaterialData)
    VisualStyle,            // VISUALSTYLE (in ACAD_VISUALSTYLE; VisualStyleData)
    // --- 9.11 render, lights and sun ---
    RenderSettings,         // RENDERSETTINGS (the base of the presets, AcDbRenderSettings; RenderSettingsData)
    MentalRayRenderSettings, // MENTALRAYRENDERSETTINGS (root ACAD_RENDER_ACTIVE_SETTINGS; MentalRayRenderSettingsData)
    RenderEnvironment,      // RENDERENVIRONMENT (root ACAD_RENDER_ENVIRONMENT; RenderEnvironmentData)
    RenderGlobal,           // RENDERGLOBAL (root ACAD_RENDER_GLOBAL; RenderGlobalData)
    LightList,              // LIGHTLIST (root ACAD_LIGHT; LightListData)
    SunStudy,               // SUNSTUDY (AcDbSunStudy; SunStudyData)
    // --- 9.10 ACDBASSOC framework ---
    AssocAction,                            // ACDBASSOCACTION
    AssocNetwork,                           // ACDBASSOCNETWORK
    AssocDependency,                        // ACDBASSOCDEPENDENCY
    AssocGeomDependency,                    // ACDBASSOCGEOMDEPENDENCY
    AssocPersSubentManager,                 // ACDBASSOCPERSSUBENTMANAGER
    PersSubentManager,                      // ACDBPERSSUBENTMANAGER
    AssocPathActionParam,                   // ACDBASSOCPATHACTIONPARAM
    AssocVertexActionParam,                 // ACDBASSOCVERTEXACTIONPARAM
    AssocOsnapPointRefActionParam,          // ACDBASSOCOSNAPPOINTREFACTIONPARAM
    AssocEdgeActionParam,                   // ACDBASSOCEDGEACTIONPARAM
    AssocExtrudedSurfaceActionBody,         // ACDBASSOCEXTRUDEDSURFACEACTIONBODY
    AssocLoftedSurfaceActionBody,           // ACDBASSOCLOFTEDSURFACEACTIONBODY
    AssocRevolvedSurfaceActionBody,         // ACDBASSOCREVOLVEDSURFACEACTIONBODY
    AssocSweptSurfaceActionBody,            // ACDBASSOCSWEPTSURFACEACTIONBODY
    AssocPlaneSurfaceActionBody,            // ACDBASSOCPLANESURFACEACTIONBODY
    AssocAlignedDimActionBody,              // ACDBASSOCALIGNEDDIMACTIONBODY
    // --- 9.14 solid history ---
    ShHistory,              // ACSH_HISTORY_CLASS (a 3DSOLID's history, owned by the solid; ShHistoryData)
    ShBox,                  // ACSH_BOX_CLASS (a node of the history's graph; ShBoxData)
    ShWedge,                // ACSH_WEDGE_CLASS (ShWedgeData)
    ShSphere,               // ACSH_SPHERE_CLASS (ShSphereData)
    ShCylinder,             // ACSH_CYLINDER_CLASS (ShCylinderData)
    ShCone,                 // ACSH_CONE_CLASS (ShConeData)
    ShTorus,                // ACSH_TORUS_CLASS (ShTorusData)
    ShPyramid,              // ACSH_PYRAMID_CLASS (ShPyramidData)
    ShExtrusion,            // ACSH_EXTRUSION_CLASS (ShExtrusionData)
    ShSweep,                // ACSH_SWEEP_CLASS (ShSweepData)
    ShRevolve,              // ACSH_REVOLVE_CLASS (ShRevolveData)
    ShLoft,                 // ACSH_LOFT_CLASS (ShLoftData)
    ShBoolean,              // ACSH_BOOLEAN_CLASS (ShBooleanData)
    ShBrep,                 // ACSH_BREP_CLASS (ShBrepData)
    ShChamfer,              // ACSH_CHAMFER_CLASS (ShChamferData)
    ShFillet,               // ACSH_FILLET_CLASS (ShFilletData)
    // --- 9.15 other ACDBASSOC classes ---
    AssocVariable, // ACDBASSOCVARIABLE
    AssocValueDependency, // ACDBASSOCVALUEDEPENDENCY
    AssocDimDependencyBody, // ASSOCDIMDEPENDENCYBODY
    Assoc2dConstraintGroup, // ACDBASSOC2DCONSTRAINTGROUP
    AssocOffsetSurfaceActionBody, // ACDBASSOCOFFSETSURFACEACTIONBODY
    AssocTrimSurfaceActionBody, // ACDBASSOCTRIMSURFACEACTIONBODY
    AssocPatchSurfaceActionBody, // ACDBASSOCPATCHSURFACEACTIONBODY
    AssocNetworkSurfaceActionBody, // ACDBASSOCNETWORKSURFACEACTIONBODY
    AssocBlendSurfaceActionBody, // ACDBASSOCBLENDSURFACEACTIONBODY
    AssocExtendSurfaceActionBody, // ACDBASSOCEXTENDSURFACEACTIONBODY
    AssocFilletSurfaceActionBody, // ACDBASSOCFILLETSURFACEACTIONBODY
    AssocEdgeFilletActionBody, // ACDBASSOCEDGEFILLETACTIONBODY
    AssocEdgeChamferActionBody, // ACDBASSOCEDGECHAMFERACTIONBODY
    AssocRestoreEntityStateActionBody, // ACDBASSOCRESTOREENTITYSTATEACTIONBODY
    AssocImpliedSurfaceOrSolidActionBody, // ACDBASSOCIMPLIEDSURFACEORSOLIACTIONBODY
    AssocRotatedDimActionBody, // ACDBASSOCROTATEDDIMACTIONBODY
    Assoc3PointAngularDimActionBody, // ACDBASSOC3POINTANGULARDIMACTIONBODY
    AssocOrdinateDimActionBody, // ACDBASSOCORDINATEDIMACTIONBODY
    AssocDiametricDimActionBody, // ACDBASSOCDIAMETRICDIMACTIONBODY
    AssocRadialDimActionBody, // ACDBASSOCRADIALDIMACTIONBODY
    AssocMLeaderActionBody, // ACDBASSOCMLEADERACTIONBODY
    AssocEntityCloneActionBody, // ACDBASSOCENTITYCLONEACTIONBODY
    AssocAsmBodyActionParam, // ACDBASSOCASMBODYACTIONPARAM
    AssocFaceActionParam, // ACDBASSOCFACEACTIONPARAM
    AssocTrimmingPathActionParam, // ACDBASSOCTRIMMINGPATHACTIONPARAM
    AssocTrimmingBodyActionParam, // ACDBASSOCTRIMMINGBODYACTIONPARAM
    AssocCompoundActionParam, // ACDBASSOCCOMPOUNDACTIONPARAM
    AssocObjectActionParam, // ACDBASSOCOBJECTACTIONPARAM
    AssocArrayActionBody, // ACDBASSOCARRAYACTIONBODY
    AssocArrayModifyActionBody, // ACDBASSOCARRAYMODIFYACTIONBODY
    // --- 9.12 sections ---
    SectionManager,         // SECTION_MANAGER (the NOD's ACAD_SECTION_MANAGER; SectionManagerData)
    SectionSettings,        // SECTION_SETTINGS (a section's settings, owned by it; SectionSettingsData)
    // --- 9.17 annotation contexts and DIMASSOC ---
    TextContext,            // ACDB_TEXTOBJECTCONTEXTDATA_CLASS (TextContextData)
    LeaderContext,          // ACDB_LEADEROBJECTCONTEXTDATA_CLASS (LeaderContextData)
    MTextContext,           // ACDB_MTEXTOBJECTCONTEXTDATA_CLASS (MTextContextData)
    MTextAttributeContext,  // ACDB_MTEXTATTRIBUTEOBJECTCONTEXTDATA_CLASS (MTextAttributeContextData)
    MLeaderContext,         // ACDB_MLEADEROBJECTCONTEXTDATA_CLASS (MLeaderContextData)
    BlkRefContext,          // ACDB_BLKREFOBJECTCONTEXTDATA_CLASS (BlkRefContextData)
    AlDimContext,           // ACDB_ALDIMOBJECTCONTEXTDATA_CLASS (AlDimContextData)
    AngDimContext,          // ACDB_ANGDIMOBJECTCONTEXTDATA_CLASS (AngDimContextData)
    DmDimContext,           // ACDB_DMDIMOBJECTCONTEXTDATA_CLASS (DmDimContextData)
    OrdDimContext,          // ACDB_ORDDIMOBJECTCONTEXTDATA_CLASS (OrdDimContextData)
    RadimContext,           // ACDB_RADIMOBJECTCONTEXTDATA_CLASS (RadimContextData)
    RadimLgContext,         // ACDB_RADIMLRGOBJECTCONTEXTDATA_CLASS (RadimLgContextData)
    FcfContext,             // ACDB_FCFOBJECTCONTEXTDATA_CLASS (FcfContextData)
    AnnotScaleContext,      // ACDB_ANNOTSCALEOBJECTCONTEXTDATA_CLASS (AnnotScaleContextData)
    HatchScaleContext,      // ACDB_HATCHSCALECONTEXTDATA_CLASS (HatchScaleContextData)
    HatchViewContext,       // ACDB_HATCHVIEWCONTEXTDATA_CLASS (HatchViewContextData)
    DimAssoc,               // DIMASSOC (DimAssocData)
    // --- 9.13 dynamic blocks: the remaining parameters, grips, actions and constraints ---
    BlockAlignmentParameter,             // BLOCKALIGNMENTPARAMETER (BlockAlignmentParameterData)
    BlockAlignmentGrip,                  // BLOCKALIGNMENTGRIP (BlockAlignmentGripData)
    BlockBasepointParameter,             // BLOCKBASEPOINTPARAMETER (BlockBasepointParameterData)
    BlockFlipParameter,                  // BLOCKFLIPPARAMETER (BlockFlipParameterData)
    BlockFlipGrip,                       // BLOCKFLIPGRIP (BlockFlipGripData)
    BlockFlipAction,                     // BLOCKFLIPACTION (BlockFlipActionData)
    BlockLookupParameter,                // BLOCKLOOKUPPARAMETER (BlockLookupParameterData)
    BlockLookupGrip,                     // BLOCKLOOKUPGRIP (BlockLookupGripData)
    BlockLookupAction,                   // BLOCKLOOKUPACTION (BlockLookupActionData)
    BlockMoveAction,                     // BLOCKMOVEACTION (BlockMoveActionData)
    BlockPointParameter,                 // BLOCKPOINTPARAMETER (BlockPointParameterData)
    BlockPolarParameter,                 // BLOCKPOLARPARAMETER (BlockPolarParameterData)
    BlockPolarGrip,                      // BLOCKPOLARGRIP (BlockPolarGripData)
    BlockPolarStretchAction,             // BLOCKPOLARSTRETCHACTION (BlockPolarStretchActionData)
    BlockPropertiesTable,                // BLOCKPROPERTIESTABLE (BlockPropertiesTableData)
    BlockPropertiesTableGrip,            // BLOCKPROPERTIESTABLEGRIP (BlockPropertiesTableGripData)
    BlockRotationParameter,              // BLOCKROTATIONPARAMETER (BlockRotationParameterData)
    BlockRotationGrip,                   // BLOCKROTATIONGRIP (BlockRotationGripData)
    BlockRotateAction,                   // BLOCKROTATEACTION (BlockRotateActionData)
    BlockStretchAction,                  // BLOCKSTRETCHACTION (BlockStretchActionData)
    BlockUserParameter,                  // BLOCKUSERPARAMETER (BlockUserParameterData)
    BlockXYParameter,                    // BLOCKXYPARAMETER (BlockXYParameterData)
    BlockXYGrip,                         // BLOCKXYGRIP (BlockXYGripData)
    BlockArrayAction,                    // BLOCKARRAYACTION (BlockArrayActionData)
    BlockHorizontalConstraintParameter,  // BLOCKHORIZONTALCONSTRAINTPARAMETER (BlockHorizontalConstraintParameterData)
    BlockVerticalConstraintParameter,    // BLOCKVERTICALCONSTRAINTPARAMETER (BlockVerticalConstraintParameterData)
    BlockAlignedConstraintParameter,     // BLOCKALIGNEDCONSTRAINTPARAMETER (BlockAlignedConstraintParameterData)
    BlockLinearConstraintParameter,      // BLOCKLINEARCONSTRAINTPARAMETER (BlockLinearConstraintParameterData)
    BlockRadialConstraintParameter,      // BLOCKRADIALCONSTRAINTPARAMETER (BlockRadialConstraintParameterData)
    BlockParameterDependencyBody,        // BLOCKPARAMDEPENDENCYBODY (BlockParameterDependencyBodyData)
    DynamicBlockProxyNode,               // ACDB_DYNAMICBLOCKPROXYNODE (DynamicBlockProxyNodeData)
    // --- 9.18 Map, TC and ZWM ---
    MapManager,                              // DMMAPMANAGER (MapManagerData)
    MapStyleLibrary,                         // DMSTYLELIBRARY (MapStyleLibraryData)
    MapStyleCategory,                        // DMSTYLECATEGORY (MapStyleCategoryData)
    MapDisplayMap,                           // DMMAP (MapDisplayMapData)
    MapBaseElement,                          // DMBASEELEMENT (MapBaseElementData)
    MapDefaultStyle,                         // DMDEFAULTSTYLE (MapDefaultStyleData)
    MapStyleReference,                       // DMSTYLEREFERENCE (MapStyleReferenceData)
    MapSurrogateStyleSets,                   // DMSURROGATESTYLESETS (MapSurrogateStyleSetsData)
    MapStylizedEntitiesTable,                // DMSTYLIZEDENTITIESTABLE (MapStylizedEntitiesTableData)
    MapMetadata,                             // MAPMETADATAOBJECT (MapMetadataData)
    MapResourceManager,                      // MAPRESOURCEMANAGEROBJECT (MapResourceManagerData)
    TcOptionalData,                          // TCOPTIONALDATA (TcOptionalDataData)
    ZwmBalloonStd,                           // ZWMBALLOONSTD (ZwmBalloonStdData)
    ZwmBomStyleStd,                          // ZWMBOMSTYLESTD (ZwmBomStyleStdData)
    ZwmBreakSymStd,                          // ZWMBREAKSYMSTD (ZwmBreakSymStdData)
    ZwmCenterHoleStd,                        // ZWMCENTERHOLESTD (ZwmCenterHoleStdData)
    ZwmChamferStd,                           // ZWMCHAMFERSTD (ZwmChamferStdData)
    ZwmCircleMarkStd,                        // ZWMCIRCLEMARKSTD (ZwmCircleMarkStdData)
    ZwmColorStd,                             // ZWMCOLORSTD (ZwmColorStdData)
    ZwmDatumIdStd,                           // ZWMDATUMIDSTD (ZwmDatumIdStdData)
    ZwmDatumTargetStd,                       // ZWMDATUMTARGETSTD (ZwmDatumTargetStdData)
    ZwmDetailStd,                            // ZWMDETAILSTD (ZwmDetailStdData)
    ZwmDimStyleStd,                          // ZWMDIMSTYLESTD (ZwmDimStyleStdData)
    ZwmElevSymStd,                           // ZWMELEVSYMSTD (ZwmElevSymStdData)
    ZwmFCFrameStd,                           // ZWMFCFRAMESTD (ZwmFCFrameStdData)
    ZwmFeatureIdStd,                         // ZWMFEATUREIDSTD (ZwmFeatureIdStdData)
    ZwmFrameStyleStd,                        // ZWMFRAMESTYLESTD (ZwmFrameStyleStdData)
    ZwmHoleChartStd,                         // ZWMHOLECHARTSTD (ZwmHoleChartStdData)
    ZwmLayerStd,                             // ZWMLAYERSTD (ZwmLayerStdData)
    ZwmLineTypeStd,                          // ZWMLINETYPESTD (ZwmLineTypeStdData)
    ZwmNoteStd,                              // ZWMNOTESTD (ZwmNoteStdData)
    ZwmPartRefStd,                           // ZWMPARTREFSTD (ZwmPartRefStdData)
    ZwmSectionSymStd,                        // ZWMSECTIONSYMSTD (ZwmSectionSymStdData)
    ZwmStandard,                             // ZWMSTANDARD (ZwmStandardData)
    ZwmSurfSymStd,                           // ZWMSURFSYMSTD (ZwmSurfSymStdData)
    ZwmTaperSlopeStd,                        // ZWMTAPERSLOPESTD (ZwmTaperSlopeStdData)
    ZwmTextStyleStd,                         // ZWMTEXTSTYLESTD (ZwmTextStyleStdData)
    ZwmTitleStyleStd,                        // ZWMTITLESTYLESTD (ZwmTitleStyleStdData)
    ZwmViewDirectionStd,                     // ZWMVIEWDIRECTIONSTD (ZwmViewDirectionStdData)
    ZwmWeldingStd,                           // ZWMWELDINGSTD (ZwmWeldingStdData)
    Unknown
};

} // namespace hdxf
