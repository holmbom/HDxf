#pragma once

// Object and the data ObjectData can hold. The types it boxes are only declared here; their definitions are in
// objects_*.hpp and table.hpp.

#include "boxed.hpp"
#include "core.hpp"
#include "fwd.hpp"
#include "object_type.hpp"
#include "objects_base.hpp"

namespace hdxf {

using ObjectDataVariant = std::variant<
    DictionaryData,
    DictionaryWithDefaultData,
    XRecordData,
    GroupData,
    LayoutData,
    PlotSettings,
    DictionaryVarData,
    PlaceholderData,
    MlineStyleData,
    ImageDefData,
    ImageDefReactorData,
    RasterVariablesData,
    DbColorData,
    MLeaderStyleData,
    GeoDataData,
    WipeoutVariablesData,
    ScaleData,
    SunData,
    NavisworksModelDefData,
    UnderlayDefinitionData,         // 9.6: PDFDEFINITION, DWFDEFINITION, DGNDEFINITION
    // --- 9.9 evaluation graph and dynamic block bases ---
    EvaluationGraphData,
    Boxed<BlockGripLocationData>,
    Boxed<BlockLinearParameterData>,
    Boxed<BlockLinearGripData>,
    Boxed<BlockVisibilityParameterData>,
    Boxed<BlockVisibilityGripData>,
    Boxed<BlockScaleActionData>,
    BlockRepresentationData,
    DynamicBlockPurgePreventerData,
    RapidRtRenderSettingsData,
    RapidRtRenderEnvironmentData,
    // --- 9.3 OLE and proxy classes ---
    ProxyObjectData,
    UnknownObjectData,
    // Phase 9, step 9.4
    SortentsTableData,
    SpatialFilterData,
    LayerIndexData,
    SpatialIndexData,
    IdBufferData,
    LayerFilterData,
    // Phase 9, step 9.1
    Boxed<TableStyleData>,
    Boxed<CellStyleMapData>,
    Boxed<TableContentData>,
    Boxed<TableGeometryData>,
    // Phase 9, step 9.8 (fields and document data)
    Boxed<FieldData>,
    FieldListData,
    Boxed<DataTableData>,
    ObjectPointerData,
    VbaProjectData,
    // Phase 9, step 9.7
    Boxed<MaterialData>,
    Boxed<VisualStyleData>,
    // Phase 9, step 9.11
    Boxed<RenderSettingsData>,
    Boxed<MentalRayRenderSettingsData>,
    Boxed<RenderEnvironmentData>,
    Boxed<RenderGlobalData>,
    Boxed<LightListData>,
    Boxed<SunStudyData>,
    // Phase 9, step 9.10 (ACDBASSOC framework)
    Boxed<AssocActionData>,
    Boxed<AssocNetworkData>,
    Boxed<AssocDependencyData>,
    Boxed<AssocGeomDependencyData>,
    Boxed<AssocPersSubentManagerData>,
    Boxed<PersSubentManagerData>,
    Boxed<AssocPathActionParamData>,
    Boxed<AssocVertexActionParamData>,
    Boxed<AssocOsnapPointRefActionParamData>,
    Boxed<AssocEdgeActionParamData>,
    Boxed<AssocExtrudedSurfaceActionBodyData>,
    Boxed<AssocLoftedSurfaceActionBodyData>,
    Boxed<AssocRevolvedSurfaceActionBodyData>,
    Boxed<AssocSweptSurfaceActionBodyData>,
    Boxed<AssocPlaneSurfaceActionBodyData>,
    Boxed<AssocAlignedDimActionBodyData>,
    // Phase 9, step 9.14 (solid history)
    Boxed<ShHistoryData>,
    Boxed<ShBoxData>,
    Boxed<ShWedgeData>,
    Boxed<ShSphereData>,
    Boxed<ShCylinderData>,
    Boxed<ShConeData>,
    Boxed<ShTorusData>,
    Boxed<ShPyramidData>,
    Boxed<ShExtrusionData>,
    Boxed<ShSweepData>,
    Boxed<ShRevolveData>,
    Boxed<ShLoftData>,
    Boxed<ShBooleanData>,
    Boxed<ShBrepData>,
    Boxed<ShChamferData>,
    Boxed<ShFilletData>,
    // Phase 9, step 9.15 (other ACDBASSOC classes)
    Boxed<AssocVariableData>,
    Boxed<AssocValueDependencyData>,
    Boxed<AssocDimDependencyBodyData>,
    Boxed<Assoc2dConstraintGroupData>,
    Boxed<AssocOffsetSurfaceActionBodyData>,
    Boxed<AssocTrimSurfaceActionBodyData>,
    Boxed<AssocPatchSurfaceActionBodyData>,
    Boxed<AssocNetworkSurfaceActionBodyData>,
    Boxed<AssocBlendSurfaceActionBodyData>,
    Boxed<AssocExtendSurfaceActionBodyData>,
    Boxed<AssocFilletSurfaceActionBodyData>,
    Boxed<AssocEdgeFilletChamferActionBodyData>,
    Boxed<AssocRestoreEntityStateActionBodyData>,
    Boxed<AssocImpliedSurfaceOrSolidActionBodyData>,
    Boxed<AssocRotatedDimActionBodyData>,
    Boxed<Assoc3PointAngularDimActionBodyData>,
    Boxed<AssocOrdinateDimActionBodyData>,
    Boxed<AssocDiametricDimActionBodyData>,
    Boxed<AssocRadialDimActionBodyData>,
    Boxed<AssocMLeaderActionBodyData>,
    Boxed<AssocEntityCloneActionBodyData>,
    Boxed<AssocAsmBodyActionParamData>,
    Boxed<AssocFaceActionParamData>,
    Boxed<AssocTrimmingPathActionParamData>,
    Boxed<AssocTrimmingBodyActionParamData>,
    Boxed<AssocCompoundActionParamData>,
    Boxed<AssocObjectActionParamData>,
    Boxed<AssocArrayActionBodyData>,
    Boxed<AssocArrayModifyActionBodyData>,
    // Phase 9, step 9.12 (the view styles replace ModelDocViewStyleData here: it is their base)
    Boxed<DetailViewStyleData>,
    Boxed<SectionViewStyleData>,
    Boxed<SectionSettingsData>,
    SectionManagerData,
    // Phase 9, step 9.17 (annotation contexts and DIMASSOC)
    Boxed<TextContextData>,
    Boxed<LeaderContextData>,
    Boxed<MTextContextData>,
    Boxed<MTextAttributeContextData>,
    Boxed<MLeaderContextData>,
    Boxed<BlkRefContextData>,
    Boxed<AlDimContextData>,
    Boxed<AngDimContextData>,
    Boxed<DmDimContextData>,
    Boxed<OrdDimContextData>,
    Boxed<RadimContextData>,
    Boxed<RadimLgContextData>,
    Boxed<FcfContextData>,
    Boxed<AnnotScaleContextData>,
    Boxed<HatchScaleContextData>,
    Boxed<HatchViewContextData>,
    Boxed<DimAssocData>,
    // Phase 9, step 9.13 (the remaining dynamic block records)
    Boxed<BlockAlignmentParameterData>,
    Boxed<BlockAlignmentGripData>,
    Boxed<BlockBasepointParameterData>,
    Boxed<BlockFlipParameterData>,
    Boxed<BlockFlipGripData>,
    Boxed<BlockFlipActionData>,
    Boxed<BlockLookupParameterData>,
    Boxed<BlockLookupGripData>,
    Boxed<BlockLookupActionData>,
    Boxed<BlockMoveActionData>,
    Boxed<BlockPointParameterData>,
    Boxed<BlockPolarParameterData>,
    Boxed<BlockPolarGripData>,
    Boxed<BlockPolarStretchActionData>,
    Boxed<BlockPropertiesTableData>,
    Boxed<BlockPropertiesTableGripData>,
    Boxed<BlockRotationParameterData>,
    Boxed<BlockRotationGripData>,
    Boxed<BlockRotateActionData>,
    Boxed<BlockStretchActionData>,
    Boxed<BlockUserParameterData>,
    Boxed<BlockXYParameterData>,
    Boxed<BlockXYGripData>,
    Boxed<BlockArrayActionData>,
    Boxed<BlockHorizontalConstraintParameterData>,
    Boxed<BlockVerticalConstraintParameterData>,
    Boxed<BlockAlignedConstraintParameterData>,
    Boxed<BlockLinearConstraintParameterData>,
    Boxed<BlockRadialConstraintParameterData>,
    Boxed<BlockParameterDependencyBodyData>,
    Boxed<DynamicBlockProxyNodeData>,
    // Phase 9, step 9.18 (Map, TC and ZWM)
    Boxed<MapManagerData>,
    Boxed<MapStyleLibraryData>,
    Boxed<MapStyleCategoryData>,
    Boxed<MapDisplayMapData>,
    Boxed<MapBaseElementData>,
    Boxed<MapDefaultStyleData>,
    Boxed<MapStyleReferenceData>,
    Boxed<MapSurrogateStyleSetsData>,
    Boxed<MapStylizedEntitiesTableData>,
    Boxed<MapMetadataData>,
    Boxed<MapResourceManagerData>,
    Boxed<TcOptionalDataData>,
    Boxed<ZwmBalloonStdData>,
    Boxed<ZwmBomStyleStdData>,
    Boxed<ZwmBreakSymStdData>,
    Boxed<ZwmCenterHoleStdData>,
    Boxed<ZwmChamferStdData>,
    Boxed<ZwmCircleMarkStdData>,
    Boxed<ZwmColorStdData>,
    Boxed<ZwmDatumIdStdData>,
    Boxed<ZwmDatumTargetStdData>,
    Boxed<ZwmDetailStdData>,
    Boxed<ZwmDimStyleStdData>,
    Boxed<ZwmElevSymStdData>,
    Boxed<ZwmFCFrameStdData>,
    Boxed<ZwmFeatureIdStdData>,
    Boxed<ZwmFrameStyleStdData>,
    Boxed<ZwmHoleChartStdData>,
    Boxed<ZwmLayerStdData>,
    Boxed<ZwmLineTypeStdData>,
    Boxed<ZwmNoteStdData>,
    Boxed<ZwmPartRefStdData>,
    Boxed<ZwmSectionSymStdData>,
    Boxed<ZwmStandardData>,
    Boxed<ZwmSurfSymStdData>,
    Boxed<ZwmTaperSlopeStdData>,
    Boxed<ZwmTextStyleStdData>,
    Boxed<ZwmTitleStyleStdData>,
    Boxed<ZwmViewDirectionStdData>,
    Boxed<ZwmWeldingStdData>
>;

// The index of the alternative T in ObjectData
template <typename T>
inline constexpr std::size_t object_data_index = data_index_in<T>(static_cast<const ObjectDataVariant*>(nullptr));

// The data of an object: a std::variant whose copying, moving, destruction, construction and access go through
// object_data.cpp. With some 200 alternatives, libstdc++ writes out its nested helpers for each in every file that uses
// them, with symbol names that grow as the cube of the number of alternatives (objects of 750 MB that ld cannot link).
// std::get, std::get_if, std::holds_alternative and std::visit take it as the std::variant it is; get<T>() and
// get_if<T>() are the same without those helpers. It is assigned as a whole (`data = SomeData{...}` converts first).
struct ObjectData : ObjectDataVariant {
    ObjectData();

    // From an alternative or a value that converts to one, as std::variant converts it. The alternative is made here
    // and moved in by object_data.cpp, so that nothing of the variant itself is made in the caller's file
    template <typename T>
        requires (!std::is_same_v<std::remove_cvref_t<T>, ObjectData>) && (!DataInPlace<std::remove_cvref_t<T>>::value) &&
                 std::is_constructible_v<ObjectDataVariant, T>
    ObjectData(T&& value)
        : ObjectData(object_data_index<DataPicked<ObjectDataVariant, T>>,
                     address_of(DataPicked<ObjectDataVariant, T>(std::forward<T>(value)))) {}

    // In place, as std::variant constructs (these are made in the caller's file)
    template <typename T, typename... Args>
    explicit ObjectData(std::in_place_type_t<T> tag, Args&&... args) : ObjectDataVariant(tag, std::forward<Args>(args)...) {}
    template <std::size_t I, typename... Args>
    explicit ObjectData(std::in_place_index_t<I> tag, Args&&... args) : ObjectDataVariant(tag, std::forward<Args>(args)...) {}

    ObjectData(const ObjectData& other);
    ObjectData(ObjectData&& other) noexcept;
    ObjectData& operator=(const ObjectData& other);
    ObjectData& operator=(ObjectData&& other) noexcept;
    ~ObjectData();

    std::size_t index() const noexcept;
    bool valueless_by_exception() const noexcept;

    template <typename T>
    T& get() {
        if (T* value = get_if<T>()) return *value;
        throw std::bad_variant_access();
    }
    template <typename T>
    const T& get() const {
        if (const T* value = get_if<T>()) return *value;
        throw std::bad_variant_access();
    }
    template <typename T>
    T* get_if() noexcept {
        return const_cast<T*>(static_cast<const ObjectData*>(this)->get_if<T>());
    }
    template <typename T>
    const T* get_if() const noexcept {
        static_assert(object_data_index<T> < std::variant_size_v<ObjectDataVariant>, "not an alternative of ObjectData");
        return index() == object_data_index<T> ? static_cast<const T*>(held()) : nullptr;
    }

    // The address of the alternative held (the Boxed<T> itself for a boxed one); null when an exception left it valueless
    const void* held() const noexcept;

private:
    // Holds the alternative at index, moved from *value
    ObjectData(std::size_t index, void* value);

    template <typename A>
    static void* address_of(A&& made) { return &made; }
};

// A proxy object's class data (ProxyObjectData::class_data): the class's type and its data as that type has it — the
// subclasses of the class's own record (TCOPTIONALDATA: `type` ObjectType::TcOptionalData, `data` Boxed<TcOptionalDataData>)
struct ProxyObjectClass {
    ObjectType type = ObjectType::Unknown;
    ObjectData data;
};

struct Object {
    ObjectType type = ObjectType::Unknown;
    std::string handle;                 // 5
    std::string owner;                  // 330 before the first subclass; "0" for none
    std::vector<std::string> reactors;  // 330 in 102 {ACAD_REACTORS
    std::string xdictionary;            // 360 in 102 {ACAD_XDICTIONARY
    ObjectData data;
    std::vector<XdataList> xdata;
    RawRecord raw;                      // the record as read

    DictionaryData& as_dictionary()             { return data.get<DictionaryData>(); }
    const DictionaryData& as_dictionary() const { return data.get<DictionaryData>(); }

    DictionaryWithDefaultData& as_dictionary_with_default()
        { return data.get<DictionaryWithDefaultData>(); }
    const DictionaryWithDefaultData& as_dictionary_with_default() const
        { return data.get<DictionaryWithDefaultData>(); }

    XRecordData& as_xrecord()                   { return data.get<XRecordData>(); }
    const XRecordData& as_xrecord() const       { return data.get<XRecordData>(); }

    GroupData& as_group()                       { return data.get<GroupData>(); }
    const GroupData& as_group() const           { return data.get<GroupData>(); }

    // The AcDbModelDocViewStyle part of a detail or section view style
    ModelDocViewStyleData& as_view_style();
    const ModelDocViewStyleData& as_view_style() const;
    DetailViewStyleData& as_detail_view_style() { return *data.get<Boxed<DetailViewStyleData>>(); }
    const DetailViewStyleData& as_detail_view_style() const { return *data.get<Boxed<DetailViewStyleData>>(); }
    SectionViewStyleData& as_section_view_style() { return *data.get<Boxed<SectionViewStyleData>>(); }
    const SectionViewStyleData& as_section_view_style() const { return *data.get<Boxed<SectionViewStyleData>>(); }

    SectionSettingsData& as_section_settings() { return *data.get<Boxed<SectionSettingsData>>(); }
    const SectionSettingsData& as_section_settings() const { return *data.get<Boxed<SectionSettingsData>>(); }
    // Phase 9, step 9.18: AutoCAD Map, TurboCAD and ZWCAD records
    MapManagerData& as_map_manager() { return *data.get<Boxed<MapManagerData>>(); }
    const MapManagerData& as_map_manager() const { return *data.get<Boxed<MapManagerData>>(); }
    MapStyleLibraryData& as_map_style_library() { return *data.get<Boxed<MapStyleLibraryData>>(); }
    const MapStyleLibraryData& as_map_style_library() const { return *data.get<Boxed<MapStyleLibraryData>>(); }
    MapStyleCategoryData& as_map_style_category() { return *data.get<Boxed<MapStyleCategoryData>>(); }
    const MapStyleCategoryData& as_map_style_category() const { return *data.get<Boxed<MapStyleCategoryData>>(); }
    MapDisplayMapData& as_map_display_map() { return *data.get<Boxed<MapDisplayMapData>>(); }
    const MapDisplayMapData& as_map_display_map() const { return *data.get<Boxed<MapDisplayMapData>>(); }
    MapBaseElementData& as_map_base_element() { return *data.get<Boxed<MapBaseElementData>>(); }
    const MapBaseElementData& as_map_base_element() const { return *data.get<Boxed<MapBaseElementData>>(); }
    MapDefaultStyleData& as_map_default_style() { return *data.get<Boxed<MapDefaultStyleData>>(); }
    const MapDefaultStyleData& as_map_default_style() const { return *data.get<Boxed<MapDefaultStyleData>>(); }
    MapStyleReferenceData& as_map_style_reference() { return *data.get<Boxed<MapStyleReferenceData>>(); }
    const MapStyleReferenceData& as_map_style_reference() const { return *data.get<Boxed<MapStyleReferenceData>>(); }
    MapSurrogateStyleSetsData& as_map_surrogate_style_sets() { return *data.get<Boxed<MapSurrogateStyleSetsData>>(); }
    const MapSurrogateStyleSetsData& as_map_surrogate_style_sets() const { return *data.get<Boxed<MapSurrogateStyleSetsData>>(); }
    MapStylizedEntitiesTableData& as_map_stylized_entities_table() { return *data.get<Boxed<MapStylizedEntitiesTableData>>(); }
    const MapStylizedEntitiesTableData& as_map_stylized_entities_table() const { return *data.get<Boxed<MapStylizedEntitiesTableData>>(); }
    MapMetadataData& as_map_metadata() { return *data.get<Boxed<MapMetadataData>>(); }
    const MapMetadataData& as_map_metadata() const { return *data.get<Boxed<MapMetadataData>>(); }
    MapResourceManagerData& as_map_resource_manager() { return *data.get<Boxed<MapResourceManagerData>>(); }
    const MapResourceManagerData& as_map_resource_manager() const { return *data.get<Boxed<MapResourceManagerData>>(); }
    TcOptionalDataData& as_tc_optional_data() { return *data.get<Boxed<TcOptionalDataData>>(); }
    const TcOptionalDataData& as_tc_optional_data() const { return *data.get<Boxed<TcOptionalDataData>>(); }
    ZwmBalloonStdData& as_zwm_balloon_std() { return *data.get<Boxed<ZwmBalloonStdData>>(); }
    const ZwmBalloonStdData& as_zwm_balloon_std() const { return *data.get<Boxed<ZwmBalloonStdData>>(); }
    ZwmBomStyleStdData& as_zwm_bom_style_std() { return *data.get<Boxed<ZwmBomStyleStdData>>(); }
    const ZwmBomStyleStdData& as_zwm_bom_style_std() const { return *data.get<Boxed<ZwmBomStyleStdData>>(); }
    ZwmBreakSymStdData& as_zwm_break_sym_std() { return *data.get<Boxed<ZwmBreakSymStdData>>(); }
    const ZwmBreakSymStdData& as_zwm_break_sym_std() const { return *data.get<Boxed<ZwmBreakSymStdData>>(); }
    ZwmCenterHoleStdData& as_zwm_center_hole_std() { return *data.get<Boxed<ZwmCenterHoleStdData>>(); }
    const ZwmCenterHoleStdData& as_zwm_center_hole_std() const { return *data.get<Boxed<ZwmCenterHoleStdData>>(); }
    ZwmChamferStdData& as_zwm_chamfer_std() { return *data.get<Boxed<ZwmChamferStdData>>(); }
    const ZwmChamferStdData& as_zwm_chamfer_std() const { return *data.get<Boxed<ZwmChamferStdData>>(); }
    ZwmCircleMarkStdData& as_zwm_circle_mark_std() { return *data.get<Boxed<ZwmCircleMarkStdData>>(); }
    const ZwmCircleMarkStdData& as_zwm_circle_mark_std() const { return *data.get<Boxed<ZwmCircleMarkStdData>>(); }
    ZwmColorStdData& as_zwm_color_std() { return *data.get<Boxed<ZwmColorStdData>>(); }
    const ZwmColorStdData& as_zwm_color_std() const { return *data.get<Boxed<ZwmColorStdData>>(); }
    ZwmDatumIdStdData& as_zwm_datum_id_std() { return *data.get<Boxed<ZwmDatumIdStdData>>(); }
    const ZwmDatumIdStdData& as_zwm_datum_id_std() const { return *data.get<Boxed<ZwmDatumIdStdData>>(); }
    ZwmDatumTargetStdData& as_zwm_datum_target_std() { return *data.get<Boxed<ZwmDatumTargetStdData>>(); }
    const ZwmDatumTargetStdData& as_zwm_datum_target_std() const { return *data.get<Boxed<ZwmDatumTargetStdData>>(); }
    ZwmDetailStdData& as_zwm_detail_std() { return *data.get<Boxed<ZwmDetailStdData>>(); }
    const ZwmDetailStdData& as_zwm_detail_std() const { return *data.get<Boxed<ZwmDetailStdData>>(); }
    ZwmDimStyleStdData& as_zwm_dim_style_std() { return *data.get<Boxed<ZwmDimStyleStdData>>(); }
    const ZwmDimStyleStdData& as_zwm_dim_style_std() const { return *data.get<Boxed<ZwmDimStyleStdData>>(); }
    ZwmElevSymStdData& as_zwm_elev_sym_std() { return *data.get<Boxed<ZwmElevSymStdData>>(); }
    const ZwmElevSymStdData& as_zwm_elev_sym_std() const { return *data.get<Boxed<ZwmElevSymStdData>>(); }
    ZwmFCFrameStdData& as_zwm_fc_frame_std() { return *data.get<Boxed<ZwmFCFrameStdData>>(); }
    const ZwmFCFrameStdData& as_zwm_fc_frame_std() const { return *data.get<Boxed<ZwmFCFrameStdData>>(); }
    ZwmFeatureIdStdData& as_zwm_feature_id_std() { return *data.get<Boxed<ZwmFeatureIdStdData>>(); }
    const ZwmFeatureIdStdData& as_zwm_feature_id_std() const { return *data.get<Boxed<ZwmFeatureIdStdData>>(); }
    ZwmFrameStyleStdData& as_zwm_frame_style_std() { return *data.get<Boxed<ZwmFrameStyleStdData>>(); }
    const ZwmFrameStyleStdData& as_zwm_frame_style_std() const { return *data.get<Boxed<ZwmFrameStyleStdData>>(); }
    ZwmHoleChartStdData& as_zwm_hole_chart_std() { return *data.get<Boxed<ZwmHoleChartStdData>>(); }
    const ZwmHoleChartStdData& as_zwm_hole_chart_std() const { return *data.get<Boxed<ZwmHoleChartStdData>>(); }
    ZwmLayerStdData& as_zwm_layer_std() { return *data.get<Boxed<ZwmLayerStdData>>(); }
    const ZwmLayerStdData& as_zwm_layer_std() const { return *data.get<Boxed<ZwmLayerStdData>>(); }
    ZwmLineTypeStdData& as_zwm_line_type_std() { return *data.get<Boxed<ZwmLineTypeStdData>>(); }
    const ZwmLineTypeStdData& as_zwm_line_type_std() const { return *data.get<Boxed<ZwmLineTypeStdData>>(); }
    ZwmNoteStdData& as_zwm_note_std() { return *data.get<Boxed<ZwmNoteStdData>>(); }
    const ZwmNoteStdData& as_zwm_note_std() const { return *data.get<Boxed<ZwmNoteStdData>>(); }
    ZwmPartRefStdData& as_zwm_part_ref_std() { return *data.get<Boxed<ZwmPartRefStdData>>(); }
    const ZwmPartRefStdData& as_zwm_part_ref_std() const { return *data.get<Boxed<ZwmPartRefStdData>>(); }
    ZwmSectionSymStdData& as_zwm_section_sym_std() { return *data.get<Boxed<ZwmSectionSymStdData>>(); }
    const ZwmSectionSymStdData& as_zwm_section_sym_std() const { return *data.get<Boxed<ZwmSectionSymStdData>>(); }
    ZwmStandardData& as_zwm_standard() { return *data.get<Boxed<ZwmStandardData>>(); }
    const ZwmStandardData& as_zwm_standard() const { return *data.get<Boxed<ZwmStandardData>>(); }
    ZwmSurfSymStdData& as_zwm_surf_sym_std() { return *data.get<Boxed<ZwmSurfSymStdData>>(); }
    const ZwmSurfSymStdData& as_zwm_surf_sym_std() const { return *data.get<Boxed<ZwmSurfSymStdData>>(); }
    ZwmTaperSlopeStdData& as_zwm_taper_slope_std() { return *data.get<Boxed<ZwmTaperSlopeStdData>>(); }
    const ZwmTaperSlopeStdData& as_zwm_taper_slope_std() const { return *data.get<Boxed<ZwmTaperSlopeStdData>>(); }
    ZwmTextStyleStdData& as_zwm_text_style_std() { return *data.get<Boxed<ZwmTextStyleStdData>>(); }
    const ZwmTextStyleStdData& as_zwm_text_style_std() const { return *data.get<Boxed<ZwmTextStyleStdData>>(); }
    ZwmTitleStyleStdData& as_zwm_title_style_std() { return *data.get<Boxed<ZwmTitleStyleStdData>>(); }
    const ZwmTitleStyleStdData& as_zwm_title_style_std() const { return *data.get<Boxed<ZwmTitleStyleStdData>>(); }
    ZwmViewDirectionStdData& as_zwm_view_direction_std() { return *data.get<Boxed<ZwmViewDirectionStdData>>(); }
    const ZwmViewDirectionStdData& as_zwm_view_direction_std() const { return *data.get<Boxed<ZwmViewDirectionStdData>>(); }
    ZwmWeldingStdData& as_zwm_welding_std() { return *data.get<Boxed<ZwmWeldingStdData>>(); }
    const ZwmWeldingStdData& as_zwm_welding_std() const { return *data.get<Boxed<ZwmWeldingStdData>>(); }
    SectionManagerData& as_section_manager() { return data.get<SectionManagerData>(); }
    const SectionManagerData& as_section_manager() const { return data.get<SectionManagerData>(); }

    // Phase 9, step 9.17: annotation contexts and DIMASSOC
    TextContextData& as_text_context() { return *data.get<Boxed<TextContextData>>(); }
    const TextContextData& as_text_context() const { return *data.get<Boxed<TextContextData>>(); }
    LeaderContextData& as_leader_context() { return *data.get<Boxed<LeaderContextData>>(); }
    const LeaderContextData& as_leader_context() const { return *data.get<Boxed<LeaderContextData>>(); }
    MTextContextData& as_mtext_context() { return *data.get<Boxed<MTextContextData>>(); }
    const MTextContextData& as_mtext_context() const { return *data.get<Boxed<MTextContextData>>(); }
    MTextAttributeContextData& as_mtext_attribute_context() { return *data.get<Boxed<MTextAttributeContextData>>(); }
    const MTextAttributeContextData& as_mtext_attribute_context() const { return *data.get<Boxed<MTextAttributeContextData>>(); }
    MLeaderContextData& as_mleader_context() { return *data.get<Boxed<MLeaderContextData>>(); }
    const MLeaderContextData& as_mleader_context() const { return *data.get<Boxed<MLeaderContextData>>(); }
    BlkRefContextData& as_blkref_context() { return *data.get<Boxed<BlkRefContextData>>(); }
    const BlkRefContextData& as_blkref_context() const { return *data.get<Boxed<BlkRefContextData>>(); }
    AlDimContextData& as_aldim_context() { return *data.get<Boxed<AlDimContextData>>(); }
    const AlDimContextData& as_aldim_context() const { return *data.get<Boxed<AlDimContextData>>(); }
    AngDimContextData& as_angdim_context() { return *data.get<Boxed<AngDimContextData>>(); }
    const AngDimContextData& as_angdim_context() const { return *data.get<Boxed<AngDimContextData>>(); }
    DmDimContextData& as_dmdim_context() { return *data.get<Boxed<DmDimContextData>>(); }
    const DmDimContextData& as_dmdim_context() const { return *data.get<Boxed<DmDimContextData>>(); }
    OrdDimContextData& as_orddim_context() { return *data.get<Boxed<OrdDimContextData>>(); }
    const OrdDimContextData& as_orddim_context() const { return *data.get<Boxed<OrdDimContextData>>(); }
    RadimContextData& as_radim_context() { return *data.get<Boxed<RadimContextData>>(); }
    const RadimContextData& as_radim_context() const { return *data.get<Boxed<RadimContextData>>(); }
    RadimLgContextData& as_radimlg_context() { return *data.get<Boxed<RadimLgContextData>>(); }
    const RadimLgContextData& as_radimlg_context() const { return *data.get<Boxed<RadimLgContextData>>(); }
    FcfContextData& as_fcf_context() { return *data.get<Boxed<FcfContextData>>(); }
    const FcfContextData& as_fcf_context() const { return *data.get<Boxed<FcfContextData>>(); }
    AnnotScaleContextData& as_annot_scale_context() { return *data.get<Boxed<AnnotScaleContextData>>(); }
    const AnnotScaleContextData& as_annot_scale_context() const { return *data.get<Boxed<AnnotScaleContextData>>(); }
    HatchScaleContextData& as_hatch_scale_context() { return *data.get<Boxed<HatchScaleContextData>>(); }
    const HatchScaleContextData& as_hatch_scale_context() const { return *data.get<Boxed<HatchScaleContextData>>(); }
    HatchViewContextData& as_hatch_view_context() { return *data.get<Boxed<HatchViewContextData>>(); }
    const HatchViewContextData& as_hatch_view_context() const { return *data.get<Boxed<HatchViewContextData>>(); }
    DimAssocData& as_dim_assoc() { return *data.get<Boxed<DimAssocData>>(); }
    const DimAssocData& as_dim_assoc() const { return *data.get<Boxed<DimAssocData>>(); }
    // The AcDbObjectContextData + AcDbAnnotScaleObjectContextData part of any of the 16 context objects; null for another object
    ObjectContextBase* context_base();
    const ObjectContextBase* context_base() const;
    // Phase 9, step 9.13: the remaining dynamic block parameters, grips, actions and constraints
    BlockAlignmentParameterData& as_block_alignment_parameter() { return *data.get<Boxed<BlockAlignmentParameterData>>(); }
    const BlockAlignmentParameterData& as_block_alignment_parameter() const { return *data.get<Boxed<BlockAlignmentParameterData>>(); }
    BlockAlignmentGripData& as_block_alignment_grip() { return *data.get<Boxed<BlockAlignmentGripData>>(); }
    const BlockAlignmentGripData& as_block_alignment_grip() const { return *data.get<Boxed<BlockAlignmentGripData>>(); }
    BlockBasepointParameterData& as_block_basepoint_parameter() { return *data.get<Boxed<BlockBasepointParameterData>>(); }
    const BlockBasepointParameterData& as_block_basepoint_parameter() const { return *data.get<Boxed<BlockBasepointParameterData>>(); }
    BlockFlipParameterData& as_block_flip_parameter() { return *data.get<Boxed<BlockFlipParameterData>>(); }
    const BlockFlipParameterData& as_block_flip_parameter() const { return *data.get<Boxed<BlockFlipParameterData>>(); }
    BlockFlipGripData& as_block_flip_grip() { return *data.get<Boxed<BlockFlipGripData>>(); }
    const BlockFlipGripData& as_block_flip_grip() const { return *data.get<Boxed<BlockFlipGripData>>(); }
    BlockFlipActionData& as_block_flip_action() { return *data.get<Boxed<BlockFlipActionData>>(); }
    const BlockFlipActionData& as_block_flip_action() const { return *data.get<Boxed<BlockFlipActionData>>(); }
    BlockLookupParameterData& as_block_lookup_parameter() { return *data.get<Boxed<BlockLookupParameterData>>(); }
    const BlockLookupParameterData& as_block_lookup_parameter() const { return *data.get<Boxed<BlockLookupParameterData>>(); }
    BlockLookupGripData& as_block_lookup_grip() { return *data.get<Boxed<BlockLookupGripData>>(); }
    const BlockLookupGripData& as_block_lookup_grip() const { return *data.get<Boxed<BlockLookupGripData>>(); }
    BlockLookupActionData& as_block_lookup_action() { return *data.get<Boxed<BlockLookupActionData>>(); }
    const BlockLookupActionData& as_block_lookup_action() const { return *data.get<Boxed<BlockLookupActionData>>(); }
    BlockMoveActionData& as_block_move_action() { return *data.get<Boxed<BlockMoveActionData>>(); }
    const BlockMoveActionData& as_block_move_action() const { return *data.get<Boxed<BlockMoveActionData>>(); }
    BlockPointParameterData& as_block_point_parameter() { return *data.get<Boxed<BlockPointParameterData>>(); }
    const BlockPointParameterData& as_block_point_parameter() const { return *data.get<Boxed<BlockPointParameterData>>(); }
    BlockPolarParameterData& as_block_polar_parameter() { return *data.get<Boxed<BlockPolarParameterData>>(); }
    const BlockPolarParameterData& as_block_polar_parameter() const { return *data.get<Boxed<BlockPolarParameterData>>(); }
    BlockPolarGripData& as_block_polar_grip() { return *data.get<Boxed<BlockPolarGripData>>(); }
    const BlockPolarGripData& as_block_polar_grip() const { return *data.get<Boxed<BlockPolarGripData>>(); }
    BlockPolarStretchActionData& as_block_polar_stretch_action() { return *data.get<Boxed<BlockPolarStretchActionData>>(); }
    const BlockPolarStretchActionData& as_block_polar_stretch_action() const { return *data.get<Boxed<BlockPolarStretchActionData>>(); }
    BlockPropertiesTableData& as_block_properties_table() { return *data.get<Boxed<BlockPropertiesTableData>>(); }
    const BlockPropertiesTableData& as_block_properties_table() const { return *data.get<Boxed<BlockPropertiesTableData>>(); }
    BlockPropertiesTableGripData& as_block_properties_table_grip() { return *data.get<Boxed<BlockPropertiesTableGripData>>(); }
    const BlockPropertiesTableGripData& as_block_properties_table_grip() const { return *data.get<Boxed<BlockPropertiesTableGripData>>(); }
    BlockRotationParameterData& as_block_rotation_parameter() { return *data.get<Boxed<BlockRotationParameterData>>(); }
    const BlockRotationParameterData& as_block_rotation_parameter() const { return *data.get<Boxed<BlockRotationParameterData>>(); }
    BlockRotationGripData& as_block_rotation_grip() { return *data.get<Boxed<BlockRotationGripData>>(); }
    const BlockRotationGripData& as_block_rotation_grip() const { return *data.get<Boxed<BlockRotationGripData>>(); }
    BlockRotateActionData& as_block_rotate_action() { return *data.get<Boxed<BlockRotateActionData>>(); }
    const BlockRotateActionData& as_block_rotate_action() const { return *data.get<Boxed<BlockRotateActionData>>(); }
    BlockStretchActionData& as_block_stretch_action() { return *data.get<Boxed<BlockStretchActionData>>(); }
    const BlockStretchActionData& as_block_stretch_action() const { return *data.get<Boxed<BlockStretchActionData>>(); }
    BlockUserParameterData& as_block_user_parameter() { return *data.get<Boxed<BlockUserParameterData>>(); }
    const BlockUserParameterData& as_block_user_parameter() const { return *data.get<Boxed<BlockUserParameterData>>(); }
    BlockXYParameterData& as_block_xy_parameter() { return *data.get<Boxed<BlockXYParameterData>>(); }
    const BlockXYParameterData& as_block_xy_parameter() const { return *data.get<Boxed<BlockXYParameterData>>(); }
    BlockXYGripData& as_block_xy_grip() { return *data.get<Boxed<BlockXYGripData>>(); }
    const BlockXYGripData& as_block_xy_grip() const { return *data.get<Boxed<BlockXYGripData>>(); }
    BlockArrayActionData& as_block_array_action() { return *data.get<Boxed<BlockArrayActionData>>(); }
    const BlockArrayActionData& as_block_array_action() const { return *data.get<Boxed<BlockArrayActionData>>(); }
    BlockHorizontalConstraintParameterData& as_block_horizontal_constraint_parameter() { return *data.get<Boxed<BlockHorizontalConstraintParameterData>>(); }
    const BlockHorizontalConstraintParameterData& as_block_horizontal_constraint_parameter() const { return *data.get<Boxed<BlockHorizontalConstraintParameterData>>(); }
    BlockVerticalConstraintParameterData& as_block_vertical_constraint_parameter() { return *data.get<Boxed<BlockVerticalConstraintParameterData>>(); }
    const BlockVerticalConstraintParameterData& as_block_vertical_constraint_parameter() const { return *data.get<Boxed<BlockVerticalConstraintParameterData>>(); }
    BlockAlignedConstraintParameterData& as_block_aligned_constraint_parameter() { return *data.get<Boxed<BlockAlignedConstraintParameterData>>(); }
    const BlockAlignedConstraintParameterData& as_block_aligned_constraint_parameter() const { return *data.get<Boxed<BlockAlignedConstraintParameterData>>(); }
    BlockLinearConstraintParameterData& as_block_linear_constraint_parameter() { return *data.get<Boxed<BlockLinearConstraintParameterData>>(); }
    const BlockLinearConstraintParameterData& as_block_linear_constraint_parameter() const { return *data.get<Boxed<BlockLinearConstraintParameterData>>(); }
    BlockRadialConstraintParameterData& as_block_radial_constraint_parameter() { return *data.get<Boxed<BlockRadialConstraintParameterData>>(); }
    const BlockRadialConstraintParameterData& as_block_radial_constraint_parameter() const { return *data.get<Boxed<BlockRadialConstraintParameterData>>(); }
    BlockParameterDependencyBodyData& as_block_parameter_dependency_body() { return *data.get<Boxed<BlockParameterDependencyBodyData>>(); }
    const BlockParameterDependencyBodyData& as_block_parameter_dependency_body() const { return *data.get<Boxed<BlockParameterDependencyBodyData>>(); }
    DynamicBlockProxyNodeData& as_dynamic_block_proxy_node() { return *data.get<Boxed<DynamicBlockProxyNodeData>>(); }
    const DynamicBlockProxyNodeData& as_dynamic_block_proxy_node() const { return *data.get<Boxed<DynamicBlockProxyNodeData>>(); }

    LayoutData& as_layout()                     { return data.get<LayoutData>(); }
    const LayoutData& as_layout() const         { return data.get<LayoutData>(); }

    PlotSettings& as_plot_settings()            { return data.get<PlotSettings>(); }
    const PlotSettings& as_plot_settings() const { return data.get<PlotSettings>(); }

    DictionaryVarData& as_dictionary_var()      { return data.get<DictionaryVarData>(); }
    const DictionaryVarData& as_dictionary_var() const
        { return data.get<DictionaryVarData>(); }

    MlineStyleData& as_mline_style()            { return data.get<MlineStyleData>(); }
    const MlineStyleData& as_mline_style() const { return data.get<MlineStyleData>(); }

    ImageDefData& as_image_def()                { return data.get<ImageDefData>(); }
    const ImageDefData& as_image_def() const    { return data.get<ImageDefData>(); }

    ImageDefReactorData& as_image_def_reactor() { return data.get<ImageDefReactorData>(); }
    const ImageDefReactorData& as_image_def_reactor() const
        { return data.get<ImageDefReactorData>(); }

    RasterVariablesData& as_raster_variables()  { return data.get<RasterVariablesData>(); }
    const RasterVariablesData& as_raster_variables() const
        { return data.get<RasterVariablesData>(); }

    DbColorData& as_db_color()                  { return data.get<DbColorData>(); }
    const DbColorData& as_db_color() const      { return data.get<DbColorData>(); }

    MLeaderStyleData& as_mleader_style()        { return data.get<MLeaderStyleData>(); }
    const MLeaderStyleData& as_mleader_style() const { return data.get<MLeaderStyleData>(); }

    GeoDataData& as_geodata()                   { return data.get<GeoDataData>(); }
    const GeoDataData& as_geodata() const       { return data.get<GeoDataData>(); }

    WipeoutVariablesData& as_wipeout_variables()             { return data.get<WipeoutVariablesData>(); }
    const WipeoutVariablesData& as_wipeout_variables() const { return data.get<WipeoutVariablesData>(); }

    ScaleData& as_scale()                       { return data.get<ScaleData>(); }
    const ScaleData& as_scale() const           { return data.get<ScaleData>(); }
    SunData& as_sun()                           { return data.get<SunData>(); }
    const SunData& as_sun() const               { return data.get<SunData>(); }

    NavisworksModelDefData& as_navisworks_model_def() { return data.get<NavisworksModelDefData>(); }
    const NavisworksModelDefData& as_navisworks_model_def() const { return data.get<NavisworksModelDefData>(); }
    // PDFDEFINITION, DWFDEFINITION, DGNDEFINITION (9.6)
    UnderlayDefinitionData& as_underlay_definition() { return data.get<UnderlayDefinitionData>(); }
    const UnderlayDefinitionData& as_underlay_definition() const { return data.get<UnderlayDefinitionData>(); }
    RapidRtRenderSettingsData& as_rapidrt_settings() { return data.get<RapidRtRenderSettingsData>(); }
    const RapidRtRenderSettingsData& as_rapidrt_settings() const { return data.get<RapidRtRenderSettingsData>(); }
    RapidRtRenderEnvironmentData& as_rapidrt_environment() { return data.get<RapidRtRenderEnvironmentData>(); }
    const RapidRtRenderEnvironmentData& as_rapidrt_environment() const { return data.get<RapidRtRenderEnvironmentData>(); }

    // ACAD_PROXY_OBJECT (9.3)
    ProxyObjectData& as_proxy_object()          { return data.get<ProxyObjectData>(); }
    const ProxyObjectData& as_proxy_object() const { return data.get<ProxyObjectData>(); }
    // TABLESTYLE, CELLSTYLEMAP, TABLECONTENT, TABLEGEOMETRY
    TableStyleData& as_table_style()            { return *data.get<Boxed<TableStyleData>>(); }
    const TableStyleData& as_table_style() const { return *data.get<Boxed<TableStyleData>>(); }
    CellStyleMapData& as_cell_style_map()       { return *data.get<Boxed<CellStyleMapData>>(); }
    const CellStyleMapData& as_cell_style_map() const { return *data.get<Boxed<CellStyleMapData>>(); }
    TableContentData& as_table_content()        { return *data.get<Boxed<TableContentData>>(); }
    const TableContentData& as_table_content() const { return *data.get<Boxed<TableContentData>>(); }
    TableGeometryData& as_table_geometry()      { return *data.get<Boxed<TableGeometryData>>(); }
    const TableGeometryData& as_table_geometry() const { return *data.get<Boxed<TableGeometryData>>(); }

    // Phase 9, step 9.9: the evaluation graph and the dynamic block bases
    EvaluationGraphData& as_evaluation_graph()  { return data.get<EvaluationGraphData>(); }
    const EvaluationGraphData& as_evaluation_graph() const { return data.get<EvaluationGraphData>(); }
    BlockGripLocationData& as_block_grip_location() { return *data.get<Boxed<BlockGripLocationData>>(); }
    const BlockGripLocationData& as_block_grip_location() const { return *data.get<Boxed<BlockGripLocationData>>(); }
    BlockLinearParameterData& as_block_linear_parameter() { return *data.get<Boxed<BlockLinearParameterData>>(); }
    const BlockLinearParameterData& as_block_linear_parameter() const { return *data.get<Boxed<BlockLinearParameterData>>(); }
    BlockLinearGripData& as_block_linear_grip() { return *data.get<Boxed<BlockLinearGripData>>(); }
    const BlockLinearGripData& as_block_linear_grip() const { return *data.get<Boxed<BlockLinearGripData>>(); }
    BlockVisibilityParameterData& as_block_visibility_parameter() { return *data.get<Boxed<BlockVisibilityParameterData>>(); }
    const BlockVisibilityParameterData& as_block_visibility_parameter() const { return *data.get<Boxed<BlockVisibilityParameterData>>(); }
    BlockVisibilityGripData& as_block_visibility_grip() { return *data.get<Boxed<BlockVisibilityGripData>>(); }
    const BlockVisibilityGripData& as_block_visibility_grip() const { return *data.get<Boxed<BlockVisibilityGripData>>(); }
    BlockScaleActionData& as_block_scale_action() { return *data.get<Boxed<BlockScaleActionData>>(); }
    const BlockScaleActionData& as_block_scale_action() const { return *data.get<Boxed<BlockScaleActionData>>(); }
    BlockRepresentationData& as_block_representation() { return data.get<BlockRepresentationData>(); }
    const BlockRepresentationData& as_block_representation() const { return data.get<BlockRepresentationData>(); }
    DynamicBlockPurgePreventerData& as_dynamic_block_purge_preventer() { return data.get<DynamicBlockPurgePreventerData>(); }
    const DynamicBlockPurgePreventerData& as_dynamic_block_purge_preventer() const { return data.get<DynamicBlockPurgePreventerData>(); }

    // Phase 9, step 9.7: MATERIAL and VISUALSTYLE
    MaterialData& as_material()                 { return *data.get<Boxed<MaterialData>>(); }
    const MaterialData& as_material() const     { return *data.get<Boxed<MaterialData>>(); }
    VisualStyleData& as_visual_style()          { return *data.get<Boxed<VisualStyleData>>(); }
    const VisualStyleData& as_visual_style() const { return *data.get<Boxed<VisualStyleData>>(); }

    // Phase 9, step 9.11: render settings, environment, light list and sun study
    RenderSettingsData& as_render_settings()    { return *data.get<Boxed<RenderSettingsData>>(); }
    const RenderSettingsData& as_render_settings() const { return *data.get<Boxed<RenderSettingsData>>(); }
    MentalRayRenderSettingsData& as_mentalray_settings() { return *data.get<Boxed<MentalRayRenderSettingsData>>(); }
    const MentalRayRenderSettingsData& as_mentalray_settings() const { return *data.get<Boxed<MentalRayRenderSettingsData>>(); }
    RenderEnvironmentData& as_render_environment() { return *data.get<Boxed<RenderEnvironmentData>>(); }
    const RenderEnvironmentData& as_render_environment() const { return *data.get<Boxed<RenderEnvironmentData>>(); }
    RenderGlobalData& as_render_global()        { return *data.get<Boxed<RenderGlobalData>>(); }
    const RenderGlobalData& as_render_global() const { return *data.get<Boxed<RenderGlobalData>>(); }
    LightListData& as_light_list()              { return *data.get<Boxed<LightListData>>(); }
    const LightListData& as_light_list() const  { return *data.get<Boxed<LightListData>>(); }
    SunStudyData& as_sun_study()                { return *data.get<Boxed<SunStudyData>>(); }
    const SunStudyData& as_sun_study() const    { return *data.get<Boxed<SunStudyData>>(); }
    // Phase 9, step 9.10: the ACDBASSOC framework
    AssocActionData& as_assoc_action()                           { return *data.get<Boxed<AssocActionData>>(); }
    const AssocActionData& as_assoc_action() const                     { return *data.get<Boxed<AssocActionData>>(); }
    AssocNetworkData& as_assoc_network()                          { return *data.get<Boxed<AssocNetworkData>>(); }
    const AssocNetworkData& as_assoc_network() const                    { return *data.get<Boxed<AssocNetworkData>>(); }
    AssocDependencyData& as_assoc_dependency()                       { return *data.get<Boxed<AssocDependencyData>>(); }
    const AssocDependencyData& as_assoc_dependency() const                 { return *data.get<Boxed<AssocDependencyData>>(); }
    AssocGeomDependencyData& as_assoc_geom_dependency()                  { return *data.get<Boxed<AssocGeomDependencyData>>(); }
    const AssocGeomDependencyData& as_assoc_geom_dependency() const            { return *data.get<Boxed<AssocGeomDependencyData>>(); }
    AssocPersSubentManagerData& as_assoc_pers_subent_manager()              { return *data.get<Boxed<AssocPersSubentManagerData>>(); }
    const AssocPersSubentManagerData& as_assoc_pers_subent_manager() const        { return *data.get<Boxed<AssocPersSubentManagerData>>(); }
    PersSubentManagerData& as_pers_subent_manager()                    { return *data.get<Boxed<PersSubentManagerData>>(); }
    const PersSubentManagerData& as_pers_subent_manager() const              { return *data.get<Boxed<PersSubentManagerData>>(); }
    AssocPathActionParamData& as_assoc_path_action_param()                { return *data.get<Boxed<AssocPathActionParamData>>(); }
    const AssocPathActionParamData& as_assoc_path_action_param() const          { return *data.get<Boxed<AssocPathActionParamData>>(); }
    AssocVertexActionParamData& as_assoc_vertex_action_param()              { return *data.get<Boxed<AssocVertexActionParamData>>(); }
    const AssocVertexActionParamData& as_assoc_vertex_action_param() const        { return *data.get<Boxed<AssocVertexActionParamData>>(); }
    AssocOsnapPointRefActionParamData& as_assoc_osnap_point_ref_action_param()     { return *data.get<Boxed<AssocOsnapPointRefActionParamData>>(); }
    const AssocOsnapPointRefActionParamData& as_assoc_osnap_point_ref_action_param() const { return *data.get<Boxed<AssocOsnapPointRefActionParamData>>(); }
    AssocEdgeActionParamData& as_assoc_edge_action_param()                { return *data.get<Boxed<AssocEdgeActionParamData>>(); }
    const AssocEdgeActionParamData& as_assoc_edge_action_param() const          { return *data.get<Boxed<AssocEdgeActionParamData>>(); }
    AssocExtrudedSurfaceActionBodyData& as_assoc_extruded_surface_action_body()     { return *data.get<Boxed<AssocExtrudedSurfaceActionBodyData>>(); }
    const AssocExtrudedSurfaceActionBodyData& as_assoc_extruded_surface_action_body() const { return *data.get<Boxed<AssocExtrudedSurfaceActionBodyData>>(); }
    AssocLoftedSurfaceActionBodyData& as_assoc_lofted_surface_action_body()       { return *data.get<Boxed<AssocLoftedSurfaceActionBodyData>>(); }
    const AssocLoftedSurfaceActionBodyData& as_assoc_lofted_surface_action_body() const { return *data.get<Boxed<AssocLoftedSurfaceActionBodyData>>(); }
    AssocRevolvedSurfaceActionBodyData& as_assoc_revolved_surface_action_body()     { return *data.get<Boxed<AssocRevolvedSurfaceActionBodyData>>(); }
    const AssocRevolvedSurfaceActionBodyData& as_assoc_revolved_surface_action_body() const { return *data.get<Boxed<AssocRevolvedSurfaceActionBodyData>>(); }
    AssocSweptSurfaceActionBodyData& as_assoc_swept_surface_action_body()        { return *data.get<Boxed<AssocSweptSurfaceActionBodyData>>(); }
    const AssocSweptSurfaceActionBodyData& as_assoc_swept_surface_action_body() const  { return *data.get<Boxed<AssocSweptSurfaceActionBodyData>>(); }
    AssocPlaneSurfaceActionBodyData& as_assoc_plane_surface_action_body()        { return *data.get<Boxed<AssocPlaneSurfaceActionBodyData>>(); }
    const AssocPlaneSurfaceActionBodyData& as_assoc_plane_surface_action_body() const  { return *data.get<Boxed<AssocPlaneSurfaceActionBodyData>>(); }
    AssocAlignedDimActionBodyData& as_assoc_aligned_dim_action_body()          { return *data.get<Boxed<AssocAlignedDimActionBodyData>>(); }
    const AssocAlignedDimActionBodyData& as_assoc_aligned_dim_action_body() const    { return *data.get<Boxed<AssocAlignedDimActionBodyData>>(); }
    // Phase 9, step 9.14: solid history
    ShHistoryData& as_sh_history() { return *data.get<Boxed<ShHistoryData>>(); }
    const ShHistoryData& as_sh_history() const { return *data.get<Boxed<ShHistoryData>>(); }
    ShBoxData& as_sh_box() { return *data.get<Boxed<ShBoxData>>(); }
    const ShBoxData& as_sh_box() const { return *data.get<Boxed<ShBoxData>>(); }
    ShWedgeData& as_sh_wedge() { return *data.get<Boxed<ShWedgeData>>(); }
    const ShWedgeData& as_sh_wedge() const { return *data.get<Boxed<ShWedgeData>>(); }
    ShSphereData& as_sh_sphere() { return *data.get<Boxed<ShSphereData>>(); }
    const ShSphereData& as_sh_sphere() const { return *data.get<Boxed<ShSphereData>>(); }
    ShCylinderData& as_sh_cylinder() { return *data.get<Boxed<ShCylinderData>>(); }
    const ShCylinderData& as_sh_cylinder() const { return *data.get<Boxed<ShCylinderData>>(); }
    ShConeData& as_sh_cone() { return *data.get<Boxed<ShConeData>>(); }
    const ShConeData& as_sh_cone() const { return *data.get<Boxed<ShConeData>>(); }
    ShTorusData& as_sh_torus() { return *data.get<Boxed<ShTorusData>>(); }
    const ShTorusData& as_sh_torus() const { return *data.get<Boxed<ShTorusData>>(); }
    ShPyramidData& as_sh_pyramid() { return *data.get<Boxed<ShPyramidData>>(); }
    const ShPyramidData& as_sh_pyramid() const { return *data.get<Boxed<ShPyramidData>>(); }
    ShExtrusionData& as_sh_extrusion() { return *data.get<Boxed<ShExtrusionData>>(); }
    const ShExtrusionData& as_sh_extrusion() const { return *data.get<Boxed<ShExtrusionData>>(); }
    ShSweepData& as_sh_sweep() { return *data.get<Boxed<ShSweepData>>(); }
    const ShSweepData& as_sh_sweep() const { return *data.get<Boxed<ShSweepData>>(); }
    ShRevolveData& as_sh_revolve() { return *data.get<Boxed<ShRevolveData>>(); }
    const ShRevolveData& as_sh_revolve() const { return *data.get<Boxed<ShRevolveData>>(); }
    ShLoftData& as_sh_loft() { return *data.get<Boxed<ShLoftData>>(); }
    const ShLoftData& as_sh_loft() const { return *data.get<Boxed<ShLoftData>>(); }
    ShBooleanData& as_sh_boolean() { return *data.get<Boxed<ShBooleanData>>(); }
    const ShBooleanData& as_sh_boolean() const { return *data.get<Boxed<ShBooleanData>>(); }
    ShBrepData& as_sh_brep() { return *data.get<Boxed<ShBrepData>>(); }
    const ShBrepData& as_sh_brep() const { return *data.get<Boxed<ShBrepData>>(); }
    ShChamferData& as_sh_chamfer() { return *data.get<Boxed<ShChamferData>>(); }
    const ShChamferData& as_sh_chamfer() const { return *data.get<Boxed<ShChamferData>>(); }
    ShFilletData& as_sh_fillet() { return *data.get<Boxed<ShFilletData>>(); }
    const ShFilletData& as_sh_fillet() const { return *data.get<Boxed<ShFilletData>>(); }
    // Phase 9, step 9.15: the other ACDBASSOC classes
    AssocVariableData& as_assoc_variable() { return *data.get<Boxed<AssocVariableData>>(); }
    const AssocVariableData& as_assoc_variable() const { return *data.get<Boxed<AssocVariableData>>(); }
    AssocValueDependencyData& as_assoc_value_dependency() { return *data.get<Boxed<AssocValueDependencyData>>(); }
    const AssocValueDependencyData& as_assoc_value_dependency() const { return *data.get<Boxed<AssocValueDependencyData>>(); }
    AssocDimDependencyBodyData& as_assoc_dim_dependency_body() { return *data.get<Boxed<AssocDimDependencyBodyData>>(); }
    const AssocDimDependencyBodyData& as_assoc_dim_dependency_body() const { return *data.get<Boxed<AssocDimDependencyBodyData>>(); }
    Assoc2dConstraintGroupData& as_assoc_2d_constraint_group() { return *data.get<Boxed<Assoc2dConstraintGroupData>>(); }
    const Assoc2dConstraintGroupData& as_assoc_2d_constraint_group() const { return *data.get<Boxed<Assoc2dConstraintGroupData>>(); }
    AssocOffsetSurfaceActionBodyData& as_assoc_offset_surface_action_body() { return *data.get<Boxed<AssocOffsetSurfaceActionBodyData>>(); }
    const AssocOffsetSurfaceActionBodyData& as_assoc_offset_surface_action_body() const { return *data.get<Boxed<AssocOffsetSurfaceActionBodyData>>(); }
    AssocTrimSurfaceActionBodyData& as_assoc_trim_surface_action_body() { return *data.get<Boxed<AssocTrimSurfaceActionBodyData>>(); }
    const AssocTrimSurfaceActionBodyData& as_assoc_trim_surface_action_body() const { return *data.get<Boxed<AssocTrimSurfaceActionBodyData>>(); }
    AssocPatchSurfaceActionBodyData& as_assoc_patch_surface_action_body() { return *data.get<Boxed<AssocPatchSurfaceActionBodyData>>(); }
    const AssocPatchSurfaceActionBodyData& as_assoc_patch_surface_action_body() const { return *data.get<Boxed<AssocPatchSurfaceActionBodyData>>(); }
    AssocNetworkSurfaceActionBodyData& as_assoc_network_surface_action_body() { return *data.get<Boxed<AssocNetworkSurfaceActionBodyData>>(); }
    const AssocNetworkSurfaceActionBodyData& as_assoc_network_surface_action_body() const { return *data.get<Boxed<AssocNetworkSurfaceActionBodyData>>(); }
    AssocBlendSurfaceActionBodyData& as_assoc_blend_surface_action_body() { return *data.get<Boxed<AssocBlendSurfaceActionBodyData>>(); }
    const AssocBlendSurfaceActionBodyData& as_assoc_blend_surface_action_body() const { return *data.get<Boxed<AssocBlendSurfaceActionBodyData>>(); }
    AssocExtendSurfaceActionBodyData& as_assoc_extend_surface_action_body() { return *data.get<Boxed<AssocExtendSurfaceActionBodyData>>(); }
    const AssocExtendSurfaceActionBodyData& as_assoc_extend_surface_action_body() const { return *data.get<Boxed<AssocExtendSurfaceActionBodyData>>(); }
    AssocFilletSurfaceActionBodyData& as_assoc_fillet_surface_action_body() { return *data.get<Boxed<AssocFilletSurfaceActionBodyData>>(); }
    const AssocFilletSurfaceActionBodyData& as_assoc_fillet_surface_action_body() const { return *data.get<Boxed<AssocFilletSurfaceActionBodyData>>(); }
    AssocEdgeFilletChamferActionBodyData& as_assoc_edge_fillet_chamfer_action_body() { return *data.get<Boxed<AssocEdgeFilletChamferActionBodyData>>(); }
    const AssocEdgeFilletChamferActionBodyData& as_assoc_edge_fillet_chamfer_action_body() const { return *data.get<Boxed<AssocEdgeFilletChamferActionBodyData>>(); }
    AssocRestoreEntityStateActionBodyData& as_assoc_restore_entity_state_action_body() { return *data.get<Boxed<AssocRestoreEntityStateActionBodyData>>(); }
    const AssocRestoreEntityStateActionBodyData& as_assoc_restore_entity_state_action_body() const { return *data.get<Boxed<AssocRestoreEntityStateActionBodyData>>(); }
    AssocImpliedSurfaceOrSolidActionBodyData& as_assoc_implied_surface_or_solid_action_body() { return *data.get<Boxed<AssocImpliedSurfaceOrSolidActionBodyData>>(); }
    const AssocImpliedSurfaceOrSolidActionBodyData& as_assoc_implied_surface_or_solid_action_body() const { return *data.get<Boxed<AssocImpliedSurfaceOrSolidActionBodyData>>(); }
    AssocRotatedDimActionBodyData& as_assoc_rotated_dim_action_body() { return *data.get<Boxed<AssocRotatedDimActionBodyData>>(); }
    const AssocRotatedDimActionBodyData& as_assoc_rotated_dim_action_body() const { return *data.get<Boxed<AssocRotatedDimActionBodyData>>(); }
    Assoc3PointAngularDimActionBodyData& as_assoc_3point_angular_dim_action_body() { return *data.get<Boxed<Assoc3PointAngularDimActionBodyData>>(); }
    const Assoc3PointAngularDimActionBodyData& as_assoc_3point_angular_dim_action_body() const { return *data.get<Boxed<Assoc3PointAngularDimActionBodyData>>(); }
    AssocOrdinateDimActionBodyData& as_assoc_ordinate_dim_action_body() { return *data.get<Boxed<AssocOrdinateDimActionBodyData>>(); }
    const AssocOrdinateDimActionBodyData& as_assoc_ordinate_dim_action_body() const { return *data.get<Boxed<AssocOrdinateDimActionBodyData>>(); }
    AssocDiametricDimActionBodyData& as_assoc_diametric_dim_action_body() { return *data.get<Boxed<AssocDiametricDimActionBodyData>>(); }
    const AssocDiametricDimActionBodyData& as_assoc_diametric_dim_action_body() const { return *data.get<Boxed<AssocDiametricDimActionBodyData>>(); }
    AssocRadialDimActionBodyData& as_assoc_radial_dim_action_body() { return *data.get<Boxed<AssocRadialDimActionBodyData>>(); }
    const AssocRadialDimActionBodyData& as_assoc_radial_dim_action_body() const { return *data.get<Boxed<AssocRadialDimActionBodyData>>(); }
    AssocMLeaderActionBodyData& as_assoc_mleader_action_body() { return *data.get<Boxed<AssocMLeaderActionBodyData>>(); }
    const AssocMLeaderActionBodyData& as_assoc_mleader_action_body() const { return *data.get<Boxed<AssocMLeaderActionBodyData>>(); }
    AssocEntityCloneActionBodyData& as_assoc_entity_clone_action_body() { return *data.get<Boxed<AssocEntityCloneActionBodyData>>(); }
    const AssocEntityCloneActionBodyData& as_assoc_entity_clone_action_body() const { return *data.get<Boxed<AssocEntityCloneActionBodyData>>(); }
    AssocAsmBodyActionParamData& as_assoc_asm_body_action_param() { return *data.get<Boxed<AssocAsmBodyActionParamData>>(); }
    const AssocAsmBodyActionParamData& as_assoc_asm_body_action_param() const { return *data.get<Boxed<AssocAsmBodyActionParamData>>(); }
    AssocFaceActionParamData& as_assoc_face_action_param() { return *data.get<Boxed<AssocFaceActionParamData>>(); }
    const AssocFaceActionParamData& as_assoc_face_action_param() const { return *data.get<Boxed<AssocFaceActionParamData>>(); }
    AssocTrimmingPathActionParamData& as_assoc_trimming_path_action_param() { return *data.get<Boxed<AssocTrimmingPathActionParamData>>(); }
    const AssocTrimmingPathActionParamData& as_assoc_trimming_path_action_param() const { return *data.get<Boxed<AssocTrimmingPathActionParamData>>(); }
    AssocTrimmingBodyActionParamData& as_assoc_trimming_body_action_param() { return *data.get<Boxed<AssocTrimmingBodyActionParamData>>(); }
    const AssocTrimmingBodyActionParamData& as_assoc_trimming_body_action_param() const { return *data.get<Boxed<AssocTrimmingBodyActionParamData>>(); }
    AssocCompoundActionParamData& as_assoc_compound_action_param() { return *data.get<Boxed<AssocCompoundActionParamData>>(); }
    const AssocCompoundActionParamData& as_assoc_compound_action_param() const { return *data.get<Boxed<AssocCompoundActionParamData>>(); }
    AssocObjectActionParamData& as_assoc_object_action_param() { return *data.get<Boxed<AssocObjectActionParamData>>(); }
    const AssocObjectActionParamData& as_assoc_object_action_param() const { return *data.get<Boxed<AssocObjectActionParamData>>(); }
    AssocArrayActionBodyData& as_assoc_array_action_body() { return *data.get<Boxed<AssocArrayActionBodyData>>(); }
    const AssocArrayActionBodyData& as_assoc_array_action_body() const { return *data.get<Boxed<AssocArrayActionBodyData>>(); }
    AssocArrayModifyActionBodyData& as_assoc_array_modify_action_body() { return *data.get<Boxed<AssocArrayModifyActionBodyData>>(); }
    const AssocArrayModifyActionBodyData& as_assoc_array_modify_action_body() const { return *data.get<Boxed<AssocArrayModifyActionBodyData>>(); }

    UnknownObjectData& as_unknown()             { return data.get<UnknownObjectData>(); }
    const UnknownObjectData& as_unknown() const { return data.get<UnknownObjectData>(); }

    // Phase 9, step 9.4: draw order, clipping and indexes
    SortentsTableData& as_sortents_table()      { return data.get<SortentsTableData>(); }
    const SortentsTableData& as_sortents_table() const { return data.get<SortentsTableData>(); }
    SpatialFilterData& as_spatial_filter()      { return data.get<SpatialFilterData>(); }
    const SpatialFilterData& as_spatial_filter() const { return data.get<SpatialFilterData>(); }
    LayerIndexData& as_layer_index()            { return data.get<LayerIndexData>(); }
    const LayerIndexData& as_layer_index() const { return data.get<LayerIndexData>(); }
    SpatialIndexData& as_spatial_index()        { return data.get<SpatialIndexData>(); }
    const SpatialIndexData& as_spatial_index() const { return data.get<SpatialIndexData>(); }
    IdBufferData& as_id_buffer()                { return data.get<IdBufferData>(); }
    const IdBufferData& as_id_buffer() const    { return data.get<IdBufferData>(); }
    LayerFilterData& as_layer_filter()          { return data.get<LayerFilterData>(); }
    const LayerFilterData& as_layer_filter() const { return data.get<LayerFilterData>(); }

    // Phase 9, step 9.8: fields and document data (DATATABLE and ACDBDATATABLE share as_data_table())
    FieldData& as_field()                       { return *data.get<Boxed<FieldData>>(); }
    const FieldData& as_field() const           { return *data.get<Boxed<FieldData>>(); }
    FieldListData& as_field_list()              { return data.get<FieldListData>(); }
    const FieldListData& as_field_list() const  { return data.get<FieldListData>(); }
    DataTableData& as_data_table()              { return *data.get<Boxed<DataTableData>>(); }
    const DataTableData& as_data_table() const  { return *data.get<Boxed<DataTableData>>(); }
    ObjectPointerData& as_object_pointer()      { return data.get<ObjectPointerData>(); }
    const ObjectPointerData& as_object_pointer() const { return data.get<ObjectPointerData>(); }
    VbaProjectData& as_vba_project()            { return data.get<VbaProjectData>(); }
    const VbaProjectData& as_vba_project() const { return data.get<VbaProjectData>(); }
};

} // namespace hdxf

// ObjectData is the std::variant it derives from for std::variant_size and std::variant_alternative
template <>
struct std::variant_size<hdxf::ObjectData> : std::variant_size<hdxf::ObjectDataVariant> {};
template <std::size_t I>
struct std::variant_alternative<I, hdxf::ObjectData> : std::variant_alternative<I, hdxf::ObjectDataVariant> {};
