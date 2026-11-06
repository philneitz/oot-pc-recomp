// SPDX-License-Identifier: GPL-3.0-or-later
// Internal native mesh replay, included by vk_exec_draw.c after its ordinary draw helpers.
// The opt-in cache contains locally compiled SPIR-V and MSH3 resource metadata, never repository assets.
// Indirect pointer resolution follows Lichii buffer_cache.h::BindGraphicsStorageBuffer; pointers are read from
// ordered GPU-visible memory before rendering begins, not from possibly stale guest bytes.
#pragma once

#define MESH_BINDINGS 64
#define MESH_C0_BYTES 0x900

typedef struct {
    GfxShaderRes reflection;   // first, so VkPipeBinding.res can refer to the common resource declaration
    uint32_t indirect;
    uint32_t texture_type;
    p_bind_buf bound;
    int kind;
} MeshResource;

typedef struct MeshModule {
    struct MeshModule *next;
    uint64_t key, native;
    uint32_t stage, regs[3], count, words;
    uint32_t *code;
    MeshResource resources[MESH_BINDINGS];
} MeshModule;

typedef struct MeshPipeline {
    struct MeshPipeline *next;
    MeshModule *mesh, *task;
    uint8_t state_key[32];
    VkPipeInfo info;
    VkPipeBinding bindings[MAX_BINDINGS];
} MeshPipeline;

static MeshModule *mesh_modules;
static MeshPipeline *mesh_pipelines;
static unsigned mesh_pipeline_count;

typedef struct { VkQueryPool pool; uint64_t program; int issued; } MeshStatistics;

static void mesh_statistics_done(void *context) {
    MeshStatistics *s = context;
    uint64_t values[3] = {0};
    if (s->issued && vkGetQueryPoolResults(vk_ctx()->device, s->pool, 0, 1, sizeof values, values, sizeof values,
                                          VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
        rt_log("mesh: statistics program %llu: fragment invocations %llu task invocations %llu mesh invocations %llu",
               (unsigned long long)s->program, (unsigned long long)values[0], (unsigned long long)values[1],
               (unsigned long long)values[2]);
    vkDestroyQueryPool(vk_ctx()->device, s->pool, NULL);
    free(s);
}

static MeshStatistics *mesh_statistics_begin(VkExec *ex) {
    static unsigned count;
    if (!getenv("OOT_MESH_STATS") || !vk_ctx()->has_mesh_queries || count >= 4) return NULL;
    MeshStatistics *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    VkQueryPoolCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS, .queryCount = 1,
        // Mesh draws cannot run inside legacy vertex/geometry/clipping statistics queries (VUID 07076).
        .pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_TASK_SHADER_INVOCATIONS_BIT_EXT | VK_QUERY_PIPELINE_STATISTIC_MESH_SHADER_INVOCATIONS_BIT_EXT};
    if (vkCreateQueryPool(vk_ctx()->device, &ci, NULL, &s->pool) != VK_SUCCESS) { free(s); return NULL; }
    count++; s->program = ex->prog->serial;
    vk_exec_pass_end(ex);   // reset commands must be outside rendering
    vkCmdResetQueryPool(ex->cb, s->pool, 0, 1);
    vk_completion_add(ex->fence, mesh_statistics_done, s);
    return s;
}

static uint64_t mesh_source_key(const uint8_t *ctl, const uint8_t *code) {
    return gfx_shader_is_nvn2(ctl) ? gfx_shader_nvn2_hash(ctl, code) : nvn2_origin(code);
}

static int mesh_program_source(const prog_t *program, int kind, const uint8_t **control, const uint8_t **code) {
    for (unsigned i = 0; i < program->nstage; i++) {
        const uint8_t *c = program->sd[i].control, *b = (const uint8_t *)(uintptr_t)program->sd[i].data;
        if (nvn2_native_kind(c, b) == kind) { *control = c; *code = b; return 1; }
    }
    return 0;
}

static MeshModule *mesh_module_get(uint64_t key, uint64_t native, unsigned stage) {
    const char *directory = getenv("OOT_MESH_CACHE");
    if (!directory || !*directory || !key || !native) return NULL;
    for (MeshModule *m = mesh_modules; m; m = m->next)
        if (m->key == key && m->stage == stage) return m->code ? m : NULL;
    MeshModule *m = calloc(1, sizeof *m);
    if (!m) rt_fatal("vk: out of memory loading a mesh module");
    m->key = key, m->stage = stage, m->next = mesh_modules, mesh_modules = m;
    VkPhysicalDeviceSubgroupProperties subgroup = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &subgroup};
    vkGetPhysicalDeviceProperties2(vk_ctx()->phys, &properties);
    VkSubgroupFeatureFlags required = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |
        VK_SUBGROUP_FEATURE_BALLOT_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT;
    VkShaderStageFlags bit = stage == 7 ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_TASK_BIT_EXT;
    if (subgroup.subgroupSize != 32 || !(subgroup.supportedStages & bit) ||
        (subgroup.supportedOperations & required) != required) {
        rt_log("mesh: cache profile requires 32-lane vote/ballot/shuffle support in stage %u", stage);
        return NULL;
    }
    char path[PLAT_PATH_MAX * 2];
    snprintf(path, sizeof path, "%s/%016llx-%s.spv.bindings", directory, (unsigned long long)key,
             stage == 7 ? "mesh" : "task");
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char magic[8]; unsigned declared, count; unsigned long long identity;
    int ok = fscanf(f, "%7s %u %llx %u %u %u %u", magic, &declared, &identity,
                    &m->regs[0], &m->regs[1], &m->regs[2], &count) == 7 && !strcmp(magic, "MSH3") &&
             declared == stage && identity == native && count <= MESH_BINDINGS;
    for (unsigned i = 0; ok && i < count; i++) {
        MeshResource *r = &m->resources[i];
        char kind; unsigned binding, bank, elements, offset = 0, written = 0;
        ok = fscanf(f, " %c %u %u", &kind, &binding, &bank) == 3 && binding == i;
        if (!ok) break;
        if (kind == 'U') {
            ok = fscanf(f, "%u", &elements) == 1 && (bank <= 1 || (bank >= 3 && bank <= 16));
            r->kind = GFX_RES_UBO, r->reflection.slot = (int16_t)bank;
        } else if (kind == 'S') {
            ok = fscanf(f, "%u %u %u %u", &offset, &elements, &written, &r->indirect) == 4 && bank == 0;
            unsigned base = stage == 6 ? 0x670 : 0x770;
            ok = ok && offset >= base && offset < base + 0x100 && (offset - base) % 16 == 0 &&
                 (r->indirect == UINT32_MAX || (r->indirect < 512 && !(r->indirect & 3)));
            r->kind = GFX_RES_SSBO, r->reflection.cbuf_slot = (int8_t)bank;
            r->reflection.handle = (uint16_t)(offset / 4), r->reflection.flags = written ? 1 : 0;
        } else if (kind == 'T') {
            ok = fscanf(f, "%u %u %u", &offset, &elements, &r->texture_type) == 3 && bank == 2 &&
                 offset >= 0x20 && offset < 0x120 && (offset - 0x20) % 8 == 0 &&
                 (r->texture_type == GFX_TEX_2D || r->texture_type == GFX_TEX_2D_ARRAY);
            r->kind = GFX_RES_TEX, r->reflection.cbuf_slot = 2;
            r->reflection.slot = (int16_t)((offset - 0x20) / 8);
            r->reflection.type = (uint16_t)(3 | (r->texture_type == GFX_TEX_2D_ARRAY ? 0x100 : 0));
            r->reflection.handle = (uint16_t)(offset / 4);
        } else ok = 0;
        ok = ok && elements == 1;
        r->reflection.binding = (uint16_t)binding, r->reflection.count = 1;
    }
    char extra;
    if (ok && fscanf(f, " %c", &extra) == 1) ok = 0;
    fclose(f);
    if (!ok || vk_ctx()->props.limits.minStorageBufferOffsetAlignment > 16) {
        rt_log("mesh: unsupported or invalid binding metadata for %016llx", (unsigned long long)key);
        return NULL;
    }
    snprintf(path, sizeof path, "%s/%016llx-%s.spv", directory, (unsigned long long)key, stage == 7 ? "mesh" : "task");
    f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
    if (size < 20 || size > 16 * 1024 * 1024 || size % 4) { fclose(f); return NULL; }
    uint32_t *code = malloc((size_t)size);
    if (!code) rt_fatal("vk: out of memory reading a mesh module");
    ok = fread(code, 1, (size_t)size, f) == (size_t)size && code[0] == 0x07230203;
    fclose(f);
    unsigned entries = 0;
    for (unsigned at = 5; ok && at < (unsigned)size / 4;) {
        unsigned len = code[at] >> 16, op = code[at] & 65535;
        if (!len || len > (unsigned)size / 4 - at) { ok = 0; break; }
        if (op == 15) {   // OpEntryPoint: TaskEXT 5364, MeshEXT 5365
            ok = len >= 4 && code[at + 1] == (stage == 6 ? 5364u : 5365u);
            entries++;
        }
        if (op == 71 && len >= 4 && code[at + 2] == 34 && code[at + 3]) ok = 0;   // DescriptorSet
        if (op == 71 && len >= 4 && code[at + 2] == 33 && code[at + 3] >= count) ok = 0;   // Binding
        at += len;
    }
    if (!ok || entries != 1) { free(code); return NULL; }
    m->native = native, m->count = count, m->words = (unsigned)size / 4, m->code = code;
    rt_log("mesh: loaded %s %016llx (%u descriptors)", stage == 7 ? "mesh" : "task", (unsigned long long)key, count);
    return m;
}

static int mesh_resources_prepare(VkExec *ex, MeshModule *m) {
    if (!m) return 1;
    // Each holder is downloaded once even when several descriptors read different pointer entries from it.
    struct { unsigned offset, bytes; uint8_t data[520]; } tables[MESH_BINDINGS];
    unsigned ntables = 0, base = m->stage == 6 ? 0x670 : 0x770;
    for (unsigned i = 0; i < m->count; i++) {
        MeshResource *r = &m->resources[i];
        if (r->kind == GFX_RES_TEX) {
            descriptor_state(ex);
            vk_texture_lock();
            uint8_t type = sampled_type_of_handle(ex, ex->tex[m->stage][r->reflection.slot]);
            vk_texture_unlock();
            if (type != r->texture_type) return 0;
        }
        if (r->kind != GFX_RES_SSBO) continue;
        unsigned offset = r->reflection.handle * 4;
        r->bound = ex->ssbo[m->stage][(offset - base) / 16];
        if (r->indirect == UINT32_MAX || !r->bound.addr) continue;
        unsigned k = 0;
        while (k < ntables && tables[k].offset != offset) k++;
        if (k == ntables) tables[ntables++] = (typeof(tables[0])){.offset = offset};
        if (tables[k].bytes < r->indirect + 8) tables[k].bytes = r->indirect + 8;
    }
    for (unsigned k = 0; k < ntables; k++) {
        p_bind_buf b = ex->ssbo[m->stage][(tables[k].offset - base) / 16];
        if ((b.size && b.size < tables[k].bytes) || !vk_exec_read_buffer(ex, b.addr, tables[k].bytes, tables[k].data)) return 0;
    }
    for (unsigned i = 0; i < m->count; i++) {
        MeshResource *r = &m->resources[i];
        if (r->kind != GFX_RES_SSBO || r->indirect == UINT32_MAX) continue;
        unsigned k = 0;
        while (k < ntables && tables[k].offset != r->reflection.handle * 4u) k++;
        r->bound = (p_bind_buf){0};
        if (k < ntables) memcpy(&r->bound.addr, tables[k].data + r->indirect, 8);
    }
    return 1;
}

static VkDescriptorBufferInfo mesh_resource_info(VkExec *ex, const VkPipeBinding *binding) {
    int stage = nvn_stage_of(binding->stage);
    const MeshResource *r = (const MeshResource *)binding->res;
    if (r->kind == GFX_RES_UBO) {
        if (r->reflection.slot == 1) {
            const uint8_t *control, *code;
            uint32_t begin, end, length;
            if (!mesh_program_source(ex->prog, stage, &control, &code)) return zero_info(65536);
            if (gfx_shader_is_nvn2(control)) {
                memcpy(&begin, control + 0x40, 4), memcpy(&end, control + 0x44, 4);
                memcpy(&length, control + 0x38, 4);
            } else {
                memcpy(&begin, control + 0x700, 4), memcpy(&end, control + 0x704, 4);
                length = end;
            }
            if (begin >= end || end > length || end - begin > 65536) return zero_info(65536);
            VkBuffer buffer; VkDeviceSize offset;
            memcpy(scratch(ex, end - begin, &buffer, &offset), code + begin, end - begin);
            return (VkDescriptorBufferInfo){buffer, offset, end - begin};
        }
        if (r->reflection.slot) return ubo_info(ex, stage, r->reflection.slot);
        uint8_t bank[MESH_C0_BYTES] = {0};
        // NVN 2 control stage tags are task 6 and mesh 7. Their observed driver storage tables start here.
        c0_table(bank + (stage == 6 ? 0x670 : 0x770), ex->ssbo[stage]);
        const float max_point = 2047.0f;
        memcpy(bank + 0x610, &max_point, 4);
        VkBuffer buffer; VkDeviceSize offset;
        memcpy(scratch(ex, sizeof bank, &buffer, &offset), bank, sizeof bank);
        return (VkDescriptorBufferInfo){buffer, offset, sizeof bank};
    }
    if (!__atomic_load_n(&LIM.ubo_max, __ATOMIC_ACQUIRE)) limits_init();
    uint64_t lead = r->bound.addr & 15u;
    uint64_t size = r->bound.size ? r->bound.size + lead : 0;
    // Lichii buffer_cache.h::StorageBufferBinding allows read-only pointer-table pools up to 64 MiB:
    // OoT's grass instances can be 12-18 MiB beyond the base. Writable pools keep the smaller bound.
    uint64_t cap = r->indirect != UINT32_MAX && !(r->reflection.flags & 1) ? 64u << 20 : 8u << 20;
    uint64_t max = LIM.sbo_max < cap ? LIM.sbo_max : cap;
    VkDescriptorBufferInfo info;
    if (r->bound.addr && bind_range(ex, r->bound.addr - lead, size, LIM.sbo_align, max,
                                   VK_MUSE_SSBO, r->reflection.flags & 1, 1, &info)) {
        if (r->reflection.flags & 1) note_written(ex, r->bound.addr - lead, info.range);
        return info;
    }
    return zero_info(1);
}

static VkShaderModule mesh_shader_module(const MeshModule *m, unsigned offset) {
    uint32_t *code = malloc((size_t)m->words * 4);
    if (!code) rt_fatal("vk: out of memory rebinding a mesh module");
    memcpy(code, m->code, (size_t)m->words * 4);
    for (unsigned at = 5; at < m->words; at += code[at] >> 16)
        if ((code[at] & 65535) == 71 && (code[at] >> 16) >= 4 && code[at + 2] == 33) code[at + 3] += offset;
    VkShaderModuleCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                   .codeSize = (size_t)m->words * 4, .pCode = code};
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(vk_ctx()->device, &ci, NULL, &module);
    free(code);
    return module;
}

static MeshPipeline *mesh_pipeline_get(const VkPipeState *state, MeshModule *mesh, MeshModule *task) {
    uint8_t key[32];
    if (!vk_pipeline_mesh_key(state, key)) return NULL;
    for (MeshPipeline *p = mesh_pipelines; p; p = p->next)
        if (p->mesh == mesh && p->task == task && !memcmp(p->state_key, key, sizeof key)) return p->info.pipeline ? p : NULL;
    // Use the ordinary graphics builder's normalized state. Comparing all NVN state also compared unused vertex
    // inputs and dynamic depth/blend values; the former 128-entry limit then dropped later colour passes silently.
    if (1 + mesh->count + (task ? task->count : 0) > MAX_BINDINGS) return NULL;
    MeshPipeline *p = calloc(1, sizeof *p);
    if (!p) rt_fatal("vk: out of memory creating a mesh pipeline");
    memcpy(p->state_key, key, sizeof key);
    p->mesh = mesh, p->task = task;
    p->next = mesh_pipelines, mesh_pipelines = p, mesh_pipeline_count++;
    p->info.bindings = p->bindings, p->info.bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS, p->info.n_sets = 1;
    p->bindings[p->info.n_bindings++] = (VkPipeBinding){.binding = 0, .stage = GFX_STAGE_MESH,
        .kind = VK_PIPE_SUPPORT, .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .count = 1, .stages = VK_SHADER_STAGE_FRAGMENT_BIT};
    unsigned offset = 1;
    MeshModule *modules[2] = {mesh, task};
    for (unsigned k = 0; k < 2; k++) if (modules[k]) {
        MeshModule *m = modules[k];
        for (unsigned i = 0; i < m->count; i++) {
            MeshResource *r = &m->resources[i];
            p->bindings[p->info.n_bindings++] = (VkPipeBinding){.binding = (uint16_t)(offset + i),
                .stage = m->stage == 7 ? GFX_STAGE_MESH : GFX_STAGE_TASK, .kind = (uint8_t)r->kind,
                .type = r->kind == GFX_RES_UBO ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : r->kind == GFX_RES_TEX ?
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .count = 1, .stages = m->stage == 7 ? VK_SHADER_STAGE_MESH_BIT_EXT : VK_SHADER_STAGE_TASK_BIT_EXT,
                .res = &r->reflection};
        }
        offset += m->count;
    }
    unsigned fragment_offset = offset - 1;
    for (int kind = 0; state->fs && kind < GFX_RES_KINDS; kind++) {
        const GfxShaderRes *resources;
        int count = gfx_shader_resources(state->fs, kind, &resources);
        for (int i = 0; i < count; i++) {
            const GfxShaderRes *r = &resources[i];
            if (p->info.n_bindings >= MAX_BINDINGS || !r->binding || r->set) return NULL;
            // Sampler-only arrays need their own descriptor and native sampler-table adapter. Treating them as
            // combined image samplers gives the driver a layout incompatible with the fragment SPIR-V.
            if (kind == GFX_RES_TEX && !(r->type & 0xff)) {
                rt_log("mesh: fragment sampler-only resources are unsupported; skipping pipeline");
                return NULL;
            }
            VkDescriptorType type = kind == GFX_RES_UBO ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER :
                kind == GFX_RES_SSBO ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : kind == GFX_RES_IMG ?
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : r->separate ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE :
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            if ((kind == GFX_RES_TEX || kind == GFX_RES_IMG) && (r->type & 0xff) == 2) return NULL;
            p->bindings[p->info.n_bindings++] = (VkPipeBinding){.binding = (uint16_t)(fragment_offset + r->binding),
                .stage = GFX_STAGE_FS, .kind = (uint8_t)kind, .type = type, .count = r->count ? r->count : 1,
                .stages = VK_SHADER_STAGE_FRAGMENT_BIT, .res = r};
        }
    }
    VkDescriptorSetLayoutBinding bindings[MAX_BINDINGS];
    for (unsigned i = 0; i < p->info.n_bindings; i++) {
        const VkPipeBinding *b = &p->bindings[i];
        bindings[i] = (VkDescriptorSetLayoutBinding){b->binding, b->type, b->count, b->stages, NULL};
    }
    VkCtx *c = vk_ctx();
    VkDescriptorSetLayoutCreateInfo si = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = p->info.n_bindings, .pBindings = bindings};
    if (vkCreateDescriptorSetLayout(c->device, &si, NULL, &p->info.set_layout) != VK_SUCCESS) return NULL;
    VkPipelineLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &p->info.set_layout};
    if (vkCreatePipelineLayout(c->device, &li, NULL, &p->info.layout) != VK_SUCCESS) return NULL;
    VkShaderModule ms = mesh_shader_module(mesh, 1), ts = task ? mesh_shader_module(task, 1 + mesh->count) : VK_NULL_HANDLE;
    if (ms && (!task || ts)) p->info.pipeline = vk_pipeline_mesh_create(state, p->info.layout, ms, ts, fragment_offset);
    if (ms) vkDestroyShaderModule(c->device, ms, NULL);
    if (ts) vkDestroyShaderModule(c->device, ts, NULL);
    return p->info.pipeline ? p : NULL;
}

static int mesh_draw(VkExec *ex, const p_draw_mesh *draw) {
    if (!getenv("OOT_MESH_CACHE") || !vk_ctx()->has_mesh_shader || ex->render_disabled || !ex->prog || draw->first || !draw->count)
        return 0;
    const uint8_t *ctl, *code;
    if (!mesh_program_source(ex->prog, 7, &ctl, &code)) return 0;
    MeshModule *mesh = mesh_module_get(mesh_source_key(ctl, code), nvn2_program_key(ctl, code), 7), *task = NULL;
    if (!mesh) return 0;
    if (mesh_program_source(ex->prog, 6, &ctl, &code)) {
        task = mesh_module_get(mesh_source_key(ctl, code), nvn2_program_key(ctl, code), 6);
        // Task translation uses only MeshInitShader (thread count and payload size). MeshShaderA/B describe the
        // mesh stage, and a shared task program can legitimately feed mesh stages with different output limits.
        if (!task || !vk_ctx()->has_task_shader || mesh->regs[2] != task->regs[2]) return 0;
    }
    static VkPhysicalDevice limits_device;
    static VkPhysicalDeviceMeshShaderPropertiesEXT limits;
    if (limits_device != vk_ctx()->phys) {
        limits = (VkPhysicalDeviceMeshShaderPropertiesEXT){.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &limits};
        vkGetPhysicalDeviceProperties2(vk_ctx()->phys, &properties);
        limits_device = vk_ctx()->phys;
    }
    uint32_t max_x = task ? limits.maxTaskWorkGroupCount[0] : limits.maxMeshWorkGroupCount[0];
    uint32_t max_total = task ? limits.maxTaskWorkGroupTotalCount : limits.maxMeshWorkGroupTotalCount;
    if (draw->count > max_x || draw->count > max_total) return 0;
    if (!mesh_resources_prepare(ex, mesh) || !mesh_resources_prepare(ex, task)) return 0;
    cb_res(ex); dummies_init();
    MeshStatistics *statistics = mesh_statistics_begin(ex);
    if (!ex->in_pass) ex->dirty |= VK_EXEC_DIRTY_VIEWPORT | VK_EXEC_DIRTY_SCISSOR;
    vk_exec_pass_begin(ex);
    VkRect2D scissor = vk_exec_scissor(ex);
    if (!scissor.extent.width || !scissor.extent.height) return 0;
    int present;
    const GfxShader *fragment = shader_for(ex, NVN_STAGE_FS, &present, NULL, 1);
    if (present && !fragment) return 0;
    uint32_t feedback = feedback_prepare(ex, fragment);
    VkPipeState state = ex->pipe;
    state.vs = state.gs = NULL, state.fs = fragment, state.prim = NVN_DRAW_PRIMITIVE_TRIANGLES;
    state.depth_minus_w = nvn_depth_minus_w() != 0, state.y_flip = 1, state.prim_restart = 0;
    state.feedback = (uint8_t)((feedback & 0xff ? 1 : 0) | (feedback & 0x100 ? 2 : 0));
    // Diagnostic only: isolate missing geometry from depth rejection or winding. Never enabled by the cache alone.
    if (getenv("OOT_MESH_TEST_RASTER")) state.ds.dt = state.ds.dw = 0, state.poly.cull = NVN_FACE_NONE;
    static unsigned state_reports;
    if (state_reports++ < 4) rt_log("mesh: state depth test %u write %u function %u cull %u", state.ds.dt, state.ds.dw,
                                  state.ds.df, state.poly.cull);
    MeshPipeline *pipeline = mesh_pipeline_get(&state, mesh, task);
    if (!pipeline) return 0;
    bind_pipeline(ex, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->info.pipeline);
    vk_pipeline_set_dynamic(ex->cb, &state); B.st_valid = 0;
    record_fixed(ex); bind_resources(ex, &pipeline->info);
    if (feedback & ex->fb_dirty) feedback_barrier(ex);
    feedback_written(ex, &state); vk_exec_oq_draw(ex);
    PFN_vkCmdDrawMeshTasksEXT issue = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(vk_ctx()->device, "vkCmdDrawMeshTasksEXT");
    if (!issue) return 0;
    if (statistics) vkCmdBeginQuery(ex->cb, statistics->pool, 0, 0), statistics->issued = 1;
    rec_draw_mesh(issue, ex->cb, draw->count, 1, 1);
    if (statistics) vkCmdEndQuery(ex->cb, statistics->pool, 0);
    ex->recorded++; vk_exec_frame.draws++;
    static unsigned reports;
    if (reports++ < 8) rt_log("mesh: issued direct program %llu with %u task groups", (unsigned long long)ex->prog->serial, draw->count);
    return 1;
}

static int mesh_draw_indirect(VkExec *ex, const p_draw_mesh_ind *draw) {
    if (!getenv("OOT_MESH_CACHE") || !vk_ctx()->has_mesh_shader || !ex->prog) return 0;
    const uint8_t *control, *code;
    if (!mesh_program_source(ex->prog, 7, &control, &code) ||
        !mesh_module_get(mesh_source_key(control, code), nvn2_program_key(control, code), 7)) return 0;
    // SDK 22's direct draw writes the MME parameters as count, first. This experimental indirect interpretation
    // still needs a populated reference comparison; it uses an ordered readback, not Vulkan EXT's 12-byte record.
    uint32_t arguments[2];
    if (!vk_exec_read_buffer(ex, draw->addr, sizeof arguments, arguments)) return 0;
    static unsigned reports;
    if (reports++ < 40) rt_log("mesh: indirect program %llu count %u first %u", (unsigned long long)ex->prog->serial,
                              arguments[0], arguments[1]);
    if (!arguments[0]) return 1;
    p_draw_mesh direct = {.first = arguments[1], .count = arguments[0], .caller = draw->caller};
    return mesh_draw(ex, &direct);
}

static void mesh_release(VkCtx *c) {
    while (mesh_pipelines) {
        MeshPipeline *p = mesh_pipelines; mesh_pipelines = p->next;
        if (p->info.pipeline) vkDestroyPipeline(c->device, p->info.pipeline, NULL);
        if (p->info.layout) vkDestroyPipelineLayout(c->device, p->info.layout, NULL);
        if (p->info.set_layout) vkDestroyDescriptorSetLayout(c->device, p->info.set_layout, NULL);
        free(p);
    }
    while (mesh_modules) { MeshModule *m = mesh_modules; mesh_modules = m->next; free(m->code); free(m); }
    mesh_pipeline_count = 0;
}
