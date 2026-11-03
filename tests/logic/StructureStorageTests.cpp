#include "TaskComposerOracle.h"
#include "projection/core/ProjectionResidencyBudget.h"
#include "projection/core/ProjectionRules.h"
#include "projection/world/ProjectionWorldLookup.h"
#include "structure/MaterialCounts.h"
#include "structure/StructureBlocks.h"
#include "structure/TaskComposer.h"
#include "structure/formats/LitematicNbt.h"

#include <cstdio>
#include <limits>
#include <random>

int runStructureStorageTests() {
    using namespace lholo::structure;
    using namespace lholo::structure::detail;
    using namespace lholo::projection::detail;
    int failures{}, checks{};
    auto check = [&](bool value, char const* reason) {
        ++checks;
        if (!value) { ++failures; std::fprintf(stderr, "StructureStorageTests: %s\n", reason); }
    };
    int token[6]{};
    auto ptr = [&](int i) { return reinterpret_cast<Block const*>(&token[i]); };
    auto nbtOwner = std::make_shared<int>();
    auto nbt = std::shared_ptr<CompoundTag const>{nbtOwner, reinterpret_cast<CompoundTag const*>(nbtOwner.get())};
    auto source = std::make_shared<LoadedStructure>();
    source->sizeX = 19; source->sizeY = 18; source->sizeZ = 21;
    source->volume = 19 * 18 * 21;
    source->regions.push_back({0,0,0,19,18,21});
    auto& cells = source->renderBlocks;
    check(cells.beginCompact(18, 21, source->volume - 1, 5), "compact initialization");
    auto a = cells.addPalette({0, 0, 0, ptr(0), nullptr, {}, 2, -1, 3});
    auto b = cells.addPalette({0, 0, 0, nullptr, ptr(1), {}, -1, 4, 5});
    auto c = cells.addPalette({0, 0, 0, ptr(2), ptr(1), nbt, 1, 4, 7});
    std::map<std::array<int, 3>, StructureBlock> oracle;
    std::mt19937 random{417};
    for (int i = 0; i < 4000; ++i) {
        int x = random() % 19, y = random() % 18, z = random() % 21;
        auto id = static_cast<std::uint32_t>(random() % 3);
        cells.appendCompact((static_cast<std::uint64_t>(x) * 18 + y) * 21 + z, id);
        // Higher palette identity emulates the later source-region winner.
        auto key = std::array{x, y, z};
        auto value = id == a ? StructureBlock{x,y,z,ptr(0),nullptr,{},2,-1,3}
            : id == b ? StructureBlock{x,y,z,nullptr,ptr(1),{},-1,4,5}
                      : StructureBlock{x,y,z,ptr(2),ptr(1),nbt,1,4,7};
        auto found = oracle.find(key);
        if (found == oracle.end() || value.sourceIndex >= found->second.sourceIndex) oracle[key] = value;
    }
    cells.finishCompact();
    check(cells.size() == oracle.size(), "overlap deduplicates cells");
    std::size_t index{};
    for (auto const& [p, expected] : oracle) {
        auto value = cells[index];
        check(std::array{value.x,value.y,value.z} == p && value.block == expected.block &&
            value.liquid == expected.liquid && value.blockEntityNbt == expected.blockEntityNbt &&
            value.materialIndex == expected.materialIndex && value.sourceIndex == expected.sourceIndex,
            "packed values preserve XYZ order, layers, source and NBT");
        check(cells.find(p[0],p[1],p[2]) == index++, "coordinate lookup matches ordinal");
    }
    check(!cells.find(-1,0,0) && !cells.find(19,0,0) && !cells.find(0,18,0) && !cells.find(0,0,21),
          "out of range coordinates never alias a row");
    std::uint64_t counted{};
    cells.forEachCountedValue([&](auto const&, auto count) { counted += count; });
    check(counted == cells.size(), "palette counts match winner cells");
    auto owners = buildMaterialUnits(cells, [](Block const*) { return lholo::block::PlacementPart{}; });
    check(owners.empty(), "ordinary material units allocate no per-cell owners");
    auto materialCounts = collectRawMaterials(cells,owners,[](std::size_t) { return true; });
    std::uint64_t bodies{}, liquids{}, expectedBodies{}, expectedLiquids{};
    for (auto const& value : materialCounts.body.entries()) bodies += value.count;
    for (auto const& value : materialCounts.liquid.entries()) liquids += value.count;
    for (auto const& [position,value] : oracle) {
        (void)position; expectedBodies += value.block != nullptr; expectedLiquids += value.liquid != nullptr;
    }
    check(bodies == expectedBodies && liquids == expectedLiquids, "material counting consumes compact values without expanding cells");
    auto expanded = std::make_shared<LoadedStructure>(*source);
    expanded->renderBlocks = {};
    for (auto value : cells) expanded->renderBlocks.push_back(value);
    std::vector<CompositionFile> compactFiles{{"upper",source,true,-4,2,3,1,2,
        {LayerAxis::Material,LayerDisplayMode::UpToCurrent,2}}, {"lower",source,true}};
    auto expandedFiles = compactFiles;
    for (auto& file : expandedFiles) file.loaded = expanded;
    auto identity = [](Block const* block,int,int) { return block; };
    auto compactTask = composeTask(compactFiles,identity), expandedTask = composeTask(expandedFiles,identity);
    check(compactTask.origin == expandedTask.origin && compactTask.loaded->renderBlocks.size() ==
          expandedTask.loaded->renderBlocks.size(), "task composition accepts compact sources");
    for (std::size_t i = 0; i < compactTask.loaded->renderBlocks.size(); ++i) {
        auto a = compactTask.loaded->renderBlocks[i], b = expandedTask.loaded->renderBlocks[i];
        check(std::array{a.x,a.y,a.z} == std::array{b.x,b.y,b.z} && a.block == b.block &&
              a.liquid == b.liquid && a.sourceIndex == b.sourceIndex && a.blockEntityNbt == b.blockEntityNbt &&
              a.materialIndex == b.materialIndex && a.liquidMaterialIndex == b.liquidMaterialIndex,
              "task priority, transform, slicing, NBT and material order match expanded oracle");
    }

    ProjectionWorldLookup::BlockTransforms transforms{{ptr(0),ptr(3)},{ptr(1),ptr(4)},{ptr(2),ptr(5)}};
    for (int mirror = 0; mirror < 3; ++mirror) for (int rotation = 0; rotation < 4; ++rotation)
        for (auto axis : {LayerAxis::X, LayerAxis::Y, LayerAxis::Material})
            for (auto mode : {LayerDisplayMode::All,LayerDisplayMode::Single,
                              LayerDisplayMode::UpToCurrent,LayerDisplayMode::FromCurrent}) {
                auto visible = [=](auto const& v) {
                    return lholo::projection::detail::isLayerVisible(axis == LayerAxis::X ? v.x : v.y,
                        mode, 3, v.materialIndex, v.liquidMaterialIndex, axis);
                };
                ProjectionWorldLookup lookup{source, {-100,200,-300}, mirror, rotation, {axis,mode,3}, transforms};
                index = 0;
                for (auto const& [p, value] : oracle) {
                    auto t = transformCell(p[0],p[1],p[2],19,21,mirror,rotation);
                    auto key = std::tuple{t[0]-100,t[1]+200,t[2]-300};
                    auto expected = visible(value) ? std::optional{index} : std::nullopt;
                    check(lookup.indexAt(key) == expected, "lookup preserves every transform and layer mode");
                    auto block = value.block ? value.block : value.liquid;
                    check(lookup.blockAt(key) == (expected ? transforms.at(block) : nullptr),
                          "lookup preserves native transformed body/liquid states");
                    ++index;
                }
                check(!lookup.indexAt({std::numeric_limits<int>::min(),0,0}) &&
                      !lookup.indexAt({std::numeric_limits<int>::max(),0,0}), "world-coordinate differences do not overflow");
            }
    ProjectionWorldLookup original{source, {0,0,0},0,0,{},transforms};
    ProjectionWorldLookup moved{source, {100,100,100},1,2,{},transforms};
    auto first = cells[0];
    check(original.indexAt({first.x,first.y,first.z}) == 0 &&
        !moved.indexAt({first.x,first.y,first.z}), "old worker snapshot survives placement replacement");

    StructureBlocks large;
    constexpr std::size_t count = 1000000;
    check(large.beginCompact(128,128,128ULL*128*128-1,1), "large compact initialization");
    auto id = large.addPalette({0,0,0,ptr(0)});
    large.reserveCompact(count);
    for (std::size_t i = 0; i < count; ++i) large.appendCompact(i,id);
    large.finishCompact();
    check(large.storageBytes() < count * 9, "one million cells stay below nine bytes per cell including row directory");
    check(large.size() == count && large.find(0,0,0) == 0 && !large.find(127,127,127), "large sparse bounds");
    StructureBlocks overlaps;
    overlaps.beginCompact(10,10,999,128);
    overlaps.reserveCompact(1000);
    for (int region = 0; region < 128; ++region) {
        auto palette = overlaps.addPalette({0,0,0,ptr(region%3)});
        for (int cell = 999; cell >= 0; --cell) overlaps.appendCompact(cell,palette);
        overlaps.mergeCompactRegion();
        check(overlaps.size() == 1000,"incremental regions retain only current winners");
    }
    overlaps.finishCompact();
    check(overlaps.storageBytes() < 100000 && overlaps[500].block == ptr(1),
          "many overlapping regions do not retain the sum of candidate cells");
    StructureBlocks replacedNbt;
    replacedNbt.beginCompact(1,1,0,2);
    auto owner = std::make_shared<int>();
    std::shared_ptr<CompoundTag const> payload{owner,reinterpret_cast<CompoundTag const*>(owner.get())};
    std::weak_ptr<CompoundTag const> oldPayload = payload;
    auto oldPalette = replacedNbt.addPalette({0,0,0,ptr(0),nullptr,payload});
    replacedNbt.appendCompact(0,oldPalette); replacedNbt.mergeCompactRegion();
    owner.reset(); payload.reset();
    auto newPalette = replacedNbt.addPalette({0,0,0,ptr(1)});
    replacedNbt.appendCompact(0,newPalette); replacedNbt.mergeCompactRegion();
    check(oldPayload.expired(),"overwritten block-entity payloads are released between regions");
    std::printf("Compact cells: %zu B vs %zu B expanded for %zu cells\n", large.storageBytes(),
        count * sizeof(StructureBlock), count);
    StructureBlocks wide;
    check(!wide.beginCompact(1,1,std::numeric_limits<std::uint64_t>::max(),3), "unpackable keys are rejected without truncation");

    // Borrowed big-endian words, including packed values crossing a word edge.
    std::string words(16,'\0');
    std::uint64_t firstWord = 3ULL << 60, secondWord = 1;
    for (int i = 0; i < 8; ++i) {
        words[i] = static_cast<char>(firstWord >> ((7-i)*8));
        words[i+8] = static_cast<char>(secondWord >> ((7-i)*8));
    }
    JavaNbtTag::LongArray view{words};
    check(view.bytes.data() == words.data() && view[0] == firstWord && view[1] == 1,
        "NBT LongArray borrows bytes and preserves endianness");
    check(packedPaletteIndex(view,12,5) == 19, "cross-word palette decoding");
    bool rejected{};
    try { JavaNbtReader{std::string_view{"\x0a\x00\x00\x0c\x00\x01s\x7f\xff\xff\xff",11}}.readRoot(); }
    catch (std::exception const&) { rejected = true; }
    check(rejected, "truncated or oversized NBT arrays fail before allocation");
    auto tagHeader = [](unsigned char type,std::string const& name) {
        return std::string(1,static_cast<char>(type)) + std::string(1,'\0') +
            std::string(1,static_cast<char>(name.size())) + name;
    };
    auto compoundList = [&](char const* name) {
        return tagHeader(9,name) + std::string{"\x0a\x00\x00\x00\x01\x00",6};
    };
    auto document = tagHeader(10,"") + tagHeader(10,"Regions") + tagHeader(10,"r") +
        compoundList("Entities") + compoundList("TileEntities") + std::string(3,'\0');
    auto tree = JavaNbtReader{document}.readRoot();
    auto const& region = std::get<JavaNbtTag::Compound>(
        std::get<JavaNbtTag::Compound>(tree.at("Regions").value).at("r").value);
    check(!region.contains("Entities") && std::get<JavaNbtTag::List>(region.at("TileEntities").value).size() == 1,
        "unused ordinary entities are skipped while block entities are preserved");
    std::vector<ProjectionResident> resident{{0,200,100},{1,300,400},{2,100,25}};
    check(selectProjectionEvictions(resident,600).empty(), "small structures keep all meshes");
    check(selectProjectionEvictions(resident,350) == std::vector<std::size_t>{1}, "geometry budget evicts farthest first");
    check(selectProjectionEvictions(resident,0) == std::vector<std::size_t>({1,0,2}), "zero budget releases all resources");
    std::printf("StructureStorageTests: %d checks, %d failures\n",checks,failures);
    return failures;
}
