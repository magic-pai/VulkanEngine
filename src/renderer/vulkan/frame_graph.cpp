#include "renderer/vulkan/frame_graph.h"

namespace se {

namespace {

constexpr u32 kFrameGraphHashOffset = 2166136261u;
constexpr u32 kFrameGraphHashPrime = 16777619u;

u32 HashByte(u32 hash, u32 value) {
    return (hash ^ (value & 0xffu)) * kFrameGraphHashPrime;
}

u32 HashU32(u32 hash, u32 value) {
    hash = HashByte(hash, value);
    hash = HashByte(hash, value >> 8u);
    hash = HashByte(hash, value >> 16u);
    return HashByte(hash, value >> 24u);
}

u32 HashString(u32 hash, std::string_view value) {
    for (const char character : value) {
        hash = HashByte(hash, static_cast<unsigned char>(character));
    }
    return hash;
}

u32 NonZeroFrameGraphId(u32 value) {
    return value != 0u ? value : 1u;
}

u32 RenderFramePassStableId(
    RenderFramePassKind kind,
    RenderFramePassStatus status,
    RenderFramePassQueue queue,
    std::string_view name
) {
    u32 hash = HashString(kFrameGraphHashOffset, "pass");
    hash = HashU32(hash, static_cast<u32>(kind));
    hash = HashU32(hash, static_cast<u32>(status));
    hash = HashU32(hash, static_cast<u32>(queue));
    hash = HashString(hash, name);
    return NonZeroFrameGraphId(hash);
}

u32 RenderGraphResourceStableId(
    RenderGraphResourceStatus status,
    RenderGraphResourceLifetime lifetime,
    std::string_view name
) {
    u32 hash = HashString(kFrameGraphHashOffset, "resource");
    hash = HashU32(hash, static_cast<u32>(status));
    hash = HashU32(hash, static_cast<u32>(lifetime));
    hash = HashString(hash, name);
    return NonZeroFrameGraphId(hash);
}

std::string_view TrimToken(std::string_view value) {
    while (!value.empty() &&
        (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
        (value.back() == ' ' ||
            value.back() == '\t' ||
            value.back() == '.' ||
            value.back() == ';')) {
        value.remove_suffix(1);
    }
    return value;
}

const RenderGraphResource* FindResourceByName(
    const RenderFrameGraphPlan& plan,
    std::string_view name
) {
    for (const RenderGraphResource& resource : plan.resources) {
        if (resource.name == name) {
            return &resource;
        }
    }
    return nullptr;
}

RenderGraphResource* FindResourceById(
    RenderFrameGraphPlan& plan,
    u32 resourceId
) {
    for (RenderGraphResource& resource : plan.resources) {
        if (resource.id == resourceId) {
            return &resource;
        }
    }
    return nullptr;
}

const RenderGraphResource* FindResourceById(
    const RenderFrameGraphPlan& plan,
    u32 resourceId
) {
    for (const RenderGraphResource& resource : plan.resources) {
        if (resource.id == resourceId) {
            return &resource;
        }
    }
    return nullptr;
}

bool ContainsWord(std::string_view text, std::string_view word) {
    return text.find(word) != std::string_view::npos;
}

bool IsLightTileFragmentConsumer(std::string_view passName) {
    return passName == "DeferredLighting" ||
        passName == "WeightedTranslucencyForwardPlus" ||
        passName == "LegacyForward3D";
}

RenderFrameGraphBarrierResourceKind InferBarrierResourceKind(
    const RenderGraphResource& resource
) {
    if (ContainsWord(resource.format, "structured") ||
        ContainsWord(resource.usage, "indirect args")) {
        return RenderFrameGraphBarrierResourceKind::Buffer;
    }
    return RenderFrameGraphBarrierResourceKind::Image;
}

RenderFrameGraphResourceAccess InferReadAccess(
    const RenderGraphResource& resource,
    const RenderFramePass& pass
) {
    if (pass.kind == RenderFramePassKind::Present &&
        resource.lifetime == RenderGraphResourceLifetime::Swapchain) {
        return RenderFrameGraphResourceAccess::Present;
    }
    if (resource.lifetime == RenderGraphResourceLifetime::Swapchain) {
        return RenderFrameGraphResourceAccess::ReadAttachment;
    }
    if (ContainsWord(resource.usage, "depth attachment") ||
        ContainsWord(resource.usage, "color attachment")) {
        return RenderFrameGraphResourceAccess::ReadAttachment;
    }
    return RenderFrameGraphResourceAccess::ReadSampled;
}

RenderFrameGraphResourceAccess InferWriteAccess(
    const RenderGraphResource& resource,
    const RenderFramePass& pass
) {
    if (pass.kind == RenderFramePassKind::Present) {
        return RenderFrameGraphResourceAccess::Present;
    }
    if (resource.lifetime == RenderGraphResourceLifetime::Swapchain) {
        return RenderFrameGraphResourceAccess::WriteColorAttachment;
    }
    if (ContainsWord(resource.usage, "depth attachment")) {
        return RenderFrameGraphResourceAccess::WriteDepthAttachment;
    }
    if (ContainsWord(resource.usage, "storage")) {
        return RenderFrameGraphResourceAccess::WriteStorage;
    }
    if (ContainsWord(resource.usage, "color attachment")) {
        return RenderFrameGraphResourceAccess::WriteColorAttachment;
    }
    return RenderFrameGraphResourceAccess::WriteStorage;
}

bool ContainsResourceRef(
    const std::vector<RenderFrameGraphResourceRef>& refs,
    u32 resourceId
) {
    for (const RenderFrameGraphResourceRef& ref : refs) {
        if (ref.resourceId == resourceId) {
            return true;
        }
    }
    return false;
}

u32 ResolveResourceRefs(
    const RenderFrameGraphPlan& plan,
    std::string_view description,
    std::vector<RenderFrameGraphResourceRef>& refs,
    const RenderFramePass& pass,
    bool writeAccess
) {
    u32 unresolvedTokenCount = 0;
    std::size_t tokenStart = 0;
    while (tokenStart <= description.size()) {
        const std::size_t separator = description.find(',', tokenStart);
        const std::size_t tokenEnd =
            separator == std::string_view::npos ? description.size() : separator;
        const std::string_view token =
            TrimToken(description.substr(tokenStart, tokenEnd - tokenStart));

        if (!token.empty()) {
            const RenderGraphResource* resource = FindResourceByName(plan, token);
            if (resource != nullptr) {
                if (!ContainsResourceRef(refs, resource->id)) {
                    refs.push_back(RenderFrameGraphResourceRef{
                        resource->id,
                        resource->name,
                        writeAccess
                            ? InferWriteAccess(*resource, pass)
                            : InferReadAccess(*resource, pass)
                    });
                }
            } else {
                ++unresolvedTokenCount;
            }
        }

        if (separator == std::string_view::npos) {
            break;
        }
        tokenStart = separator + 1;
    }
    return unresolvedTokenCount;
}

void CountAccess(
    RenderFrameGraphReferenceStats& stats,
    RenderFrameGraphResourceAccess access
) {
    switch (access) {
    case RenderFrameGraphResourceAccess::ReadSampled:
        ++stats.readSampledCount;
        return;
    case RenderFrameGraphResourceAccess::ReadAttachment:
        ++stats.readAttachmentCount;
        return;
    case RenderFrameGraphResourceAccess::WriteColorAttachment:
        ++stats.writeColorAttachmentCount;
        return;
    case RenderFrameGraphResourceAccess::WriteDepthAttachment:
        ++stats.writeDepthAttachmentCount;
        return;
    case RenderFrameGraphResourceAccess::WriteStorage:
        ++stats.writeStorageCount;
        return;
    case RenderFrameGraphResourceAccess::Present:
        ++stats.presentCount;
        return;
    }
}

void AppendValidationIssue(
    RenderFrameGraphValidation& validation,
    RenderFrameGraphValidationIssue issue
) {
    ++validation.issueCount;
    switch (issue.kind) {
    case RenderFrameGraphValidationIssueKind::UnnamedPass:
        ++validation.unnamedPassCount;
        break;
    case RenderFrameGraphValidationIssueKind::DuplicatePassId:
        ++validation.duplicatePassIdCount;
        break;
    case RenderFrameGraphValidationIssueKind::UnnamedResource:
        ++validation.unnamedResourceCount;
        break;
    case RenderFrameGraphValidationIssueKind::DuplicateResourceId:
        ++validation.duplicateResourceIdCount;
        break;
    case RenderFrameGraphValidationIssueKind::MissingResourceRef:
        ++validation.missingResourceRefCount;
        break;
    case RenderFrameGraphValidationIssueKind::ReadBeforeFirstWrite:
        ++validation.readBeforeFirstWriteCount;
        break;
    case RenderFrameGraphValidationIssueKind::UnusedPhysicalResource:
        ++validation.unusedPhysicalResourceCount;
        break;
    case RenderFrameGraphValidationIssueKind::WriteOnlyRoadmapResource:
        ++validation.writeOnlyRoadmapResourceCount;
        break;
    case RenderFrameGraphValidationIssueKind::ActivePassWritesPlannedResource:
        ++validation.activePassWritesPlannedResourceCount;
        break;
    }
    validation.issues.push_back(issue);
}

bool ContainsId(const std::vector<u32>& ids, u32 id) {
    for (const u32 existingId : ids) {
        if (existingId == id) {
            return true;
        }
    }
    return false;
}

bool HasActiveWriter(
    const RenderFrameGraphPlan& plan,
    u32 resourceId
) {
    for (const RenderFramePass& pass : plan.passes) {
        if (pass.status != RenderFramePassStatus::Active) {
            continue;
        }
        for (const RenderFrameGraphResourceRef& ref : pass.writeResources) {
            if (ref.resourceId == resourceId) {
                return true;
            }
        }
    }
    return false;
}

bool HasRoadmapWriter(
    const RenderFrameGraphPlan& plan,
    u32 resourceId
) {
    for (const RenderFramePass& pass : plan.passes) {
        if (pass.status != RenderFramePassStatus::Roadmap) {
            continue;
        }
        for (const RenderFrameGraphResourceRef& ref : pass.writeResources) {
            if (ref.resourceId == resourceId) {
                return true;
            }
        }
    }
    return false;
}

void AppendMissingResourceRefIssues(
    const RenderFrameGraphPlan& plan,
    const RenderFramePass& pass,
    std::string_view description,
    bool writeRef,
    RenderFrameGraphValidation& validation
) {
    std::size_t tokenStart = 0;
    while (tokenStart <= description.size()) {
        const std::size_t separator = description.find(',', tokenStart);
        const std::size_t tokenEnd =
            separator == std::string_view::npos ? description.size() : separator;
        const std::string_view token =
            TrimToken(description.substr(tokenStart, tokenEnd - tokenStart));

        if (!token.empty() && FindResourceByName(plan, token) == nullptr) {
            AppendValidationIssue(
                validation,
                RenderFrameGraphValidationIssue{
                    RenderFrameGraphValidationIssueKind::MissingResourceRef,
                    pass.id,
                    pass.name,
                    0u,
                    token,
                    writeRef
                }
            );
        }

        if (separator == std::string_view::npos) {
            break;
        }
        tokenStart = separator + 1;
    }
}

const RenderFramePass* FindPassById(
    const RenderFrameGraphPlan& plan,
    u32 passId
) {
    for (const RenderFramePass& pass : plan.passes) {
        if (pass.id == passId) {
            return &pass;
        }
    }
    return nullptr;
}

const RenderFrameGraphResourceRef* FindResourceRefById(
    const std::vector<RenderFrameGraphResourceRef>& refs,
    u32 resourceId
) {
    for (const RenderFrameGraphResourceRef& ref : refs) {
        if (ref.resourceId == resourceId) {
            return &ref;
        }
    }
    return nullptr;
}

bool ContainsDependency(
    const std::vector<RenderFrameGraphPassDependency>& dependencies,
    u32 passId,
    u32 resourceId,
    bool writeDependency
) {
    for (const RenderFrameGraphPassDependency& dependency : dependencies) {
        if (dependency.passId == passId &&
            dependency.resourceId == resourceId &&
            dependency.writeDependency == writeDependency) {
            return true;
        }
    }
    return false;
}

void AppendDependency(
    RenderFrameGraphPlan& plan,
    RenderFramePass& pass,
    u32 dependencyPassId,
    const RenderFrameGraphResourceRef& resourceRef,
    bool writeDependency
) {
    if (dependencyPassId == 0u || dependencyPassId == pass.id ||
        ContainsDependency(
            pass.dependencies,
            dependencyPassId,
            resourceRef.resourceId,
            writeDependency
        )) {
        return;
    }

    const RenderFramePass* dependencyPass =
        FindPassById(plan, dependencyPassId);
    pass.dependencies.push_back(RenderFrameGraphPassDependency{
        dependencyPassId,
        dependencyPass != nullptr ? dependencyPass->name : std::string_view{},
        resourceRef.resourceId,
        resourceRef.name,
        writeDependency
    });
    ++plan.dependencies.dependencyCount;
    if (writeDependency) {
        ++plan.dependencies.writeAfterWriteCount;
    } else {
        ++plan.dependencies.readAfterWriteCount;
    }
}

void RebuildFrameGraphPassDependencies(RenderFrameGraphPlan& plan) {
    RenderFrameGraphDependencyStats stats{};
    plan.dependencies = stats;
    std::vector<RenderFrameGraphResourceRef> lastWrites;
    std::vector<u32> lastWriterPassIds;

    for (RenderFramePass& pass : plan.passes) {
        pass.dependencies.clear();

        for (const RenderFrameGraphResourceRef& readRef : pass.readResources) {
            for (std::size_t index = 0; index < lastWrites.size(); ++index) {
                if (lastWrites[index].resourceId == readRef.resourceId) {
                    AppendDependency(
                        plan,
                        pass,
                        lastWriterPassIds[index],
                        readRef,
                        false
                    );
                    break;
                }
            }
        }

        for (const RenderFrameGraphResourceRef& writeRef : pass.writeResources) {
            bool updatedLastWrite = false;
            for (std::size_t index = 0; index < lastWrites.size(); ++index) {
                if (lastWrites[index].resourceId == writeRef.resourceId) {
                    AppendDependency(
                        plan,
                        pass,
                        lastWriterPassIds[index],
                        writeRef,
                        true
                    );
                    lastWrites[index] = writeRef;
                    lastWriterPassIds[index] = pass.id;
                    updatedLastWrite = true;
                    break;
                }
            }
            if (!updatedLastWrite) {
                lastWrites.push_back(writeRef);
                lastWriterPassIds.push_back(pass.id);
            }
        }
    }
}

std::string_view BarrierStageForAccess(
    RenderFrameGraphResourceAccess access,
    RenderFramePassQueue queue
) {
    if (queue == RenderFramePassQueue::Compute ||
        queue == RenderFramePassQueue::AsyncCompute) {
        return "compute shader";
    }
    if (queue == RenderFramePassQueue::Transfer) {
        return "transfer";
    }
    if (queue == RenderFramePassQueue::Present ||
        access == RenderFrameGraphResourceAccess::Present) {
        return "present";
    }

    switch (access) {
    case RenderFrameGraphResourceAccess::ReadSampled:
    case RenderFrameGraphResourceAccess::WriteStorage:
        return "fragment shader";
    case RenderFrameGraphResourceAccess::ReadAttachment:
    case RenderFrameGraphResourceAccess::WriteDepthAttachment:
        return "early/late fragment tests";
    case RenderFrameGraphResourceAccess::WriteColorAttachment:
        return "color attachment output";
    case RenderFrameGraphResourceAccess::Present:
        return "present";
    }

    return "pipeline";
}

std::string_view BarrierLayoutForAccess(
    RenderFrameGraphResourceAccess access,
    RenderFrameGraphBarrierResourceKind resourceKind
) {
    if (resourceKind == RenderFrameGraphBarrierResourceKind::Buffer) {
        return "buffer";
    }

    switch (access) {
    case RenderFrameGraphResourceAccess::ReadSampled:
        return "shader read only";
    case RenderFrameGraphResourceAccess::ReadAttachment:
        return "attachment read";
    case RenderFrameGraphResourceAccess::WriteColorAttachment:
        return "color attachment";
    case RenderFrameGraphResourceAccess::WriteDepthAttachment:
        return "depth attachment";
    case RenderFrameGraphResourceAccess::WriteStorage:
        return "general";
    case RenderFrameGraphResourceAccess::Present:
        return "present";
    }

    return "unknown";
}

bool MatchesBarrierBridge(
    const RenderFrameGraphBarrierTransition& transition,
    RenderFrameGraphBarrierBridge bridge
) {
    switch (bridge) {
    case RenderFrameGraphBarrierBridge::LightTileCullFragmentRead:
        return transition.producerPassName == "LightTileCull" &&
            IsLightTileFragmentConsumer(transition.consumerPassName) &&
            transition.resourceName == "LightTileLists" &&
            transition.resourceKind == RenderFrameGraphBarrierResourceKind::Buffer &&
            transition.srcAccess == RenderFrameGraphResourceAccess::WriteStorage &&
            transition.dstAccess == RenderFrameGraphResourceAccess::ReadSampled &&
            transition.producerQueue == RenderFramePassQueue::Compute &&
            transition.consumerQueue == RenderFramePassQueue::Graphics &&
            !transition.writeDependency;
    case RenderFrameGraphBarrierBridge::AutoExposureHistoryFragmentRead:
        return transition.producerPassName == "AutoExposureHistogramBuild" &&
            transition.consumerPassName == "HDRComposite" &&
            transition.resourceName == "AutoExposureHistory" &&
            transition.resourceKind == RenderFrameGraphBarrierResourceKind::Buffer &&
            transition.srcAccess == RenderFrameGraphResourceAccess::WriteStorage &&
            transition.dstAccess == RenderFrameGraphResourceAccess::ReadSampled &&
            transition.producerQueue == RenderFramePassQueue::Compute &&
            transition.consumerQueue == RenderFramePassQueue::Graphics &&
            !transition.writeDependency;
    }

    return false;
}

u32 CountBarrierBridgeTransitions(
    const RenderFrameGraphPlan& plan,
    RenderFrameGraphBarrierBridge bridge
) {
    u32 count = 0;
    for (const RenderFrameGraphBarrierTransition& transition :
        plan.barrierTransitions) {
        if (MatchesBarrierBridge(transition, bridge)) {
            ++count;
        }
    }
    return count;
}

void RebuildFrameGraphBarrierPlan(RenderFrameGraphPlan& plan) {
    RenderFrameGraphBarrierStats stats{};
    plan.barrierTransitions.clear();

    for (const RenderFramePass& consumerPass : plan.passes) {
        for (const RenderFrameGraphPassDependency& dependency :
            consumerPass.dependencies) {
            const RenderFramePass* producerPass =
                FindPassById(plan, dependency.passId);
            const RenderGraphResource* resource =
                FindResourceById(plan, dependency.resourceId);
            const RenderFrameGraphResourceRef* producerWriteRef =
                producerPass != nullptr
                    ? FindResourceRefById(
                        producerPass->writeResources,
                        dependency.resourceId
                    )
                    : nullptr;
            const RenderFrameGraphResourceRef* consumerReadRef =
                FindResourceRefById(
                    consumerPass.readResources,
                    dependency.resourceId
                );
            const RenderFrameGraphResourceRef* consumerWriteRef =
                FindResourceRefById(
                    consumerPass.writeResources,
                    dependency.resourceId
                );
            const RenderFrameGraphResourceRef* consumerRef =
                dependency.writeDependency ? consumerWriteRef : consumerReadRef;

            if (producerPass == nullptr ||
                resource == nullptr ||
                producerWriteRef == nullptr ||
                consumerRef == nullptr) {
                continue;
            }

            const RenderFrameGraphBarrierResourceKind resourceKind =
                InferBarrierResourceKind(*resource);
            const std::string_view oldLayout =
                BarrierLayoutForAccess(producerWriteRef->access, resourceKind);
            const std::string_view newLayout =
                BarrierLayoutForAccess(consumerRef->access, resourceKind);

            RenderFrameGraphBarrierTransition transition{};
            transition.producerPassId = producerPass->id;
            transition.producerPassName = producerPass->name;
            transition.producerQueue = producerPass->queue;
            transition.consumerPassId = consumerPass.id;
            transition.consumerPassName = consumerPass.name;
            transition.consumerQueue = consumerPass.queue;
            transition.resourceId = resource->id;
            transition.resourceName = resource->name;
            transition.resourceKind = resourceKind;
            transition.srcAccess = producerWriteRef->access;
            transition.dstAccess = consumerRef->access;
            transition.srcStage =
                BarrierStageForAccess(producerWriteRef->access, producerPass->queue);
            transition.dstStage =
                BarrierStageForAccess(consumerRef->access, consumerPass.queue);
            transition.oldLayout = oldLayout;
            transition.newLayout = newLayout;
            transition.layoutTransition = oldLayout != newLayout;
            transition.queueOwnershipTransfer =
                producerPass->queue != consumerPass.queue;
            transition.writeDependency = dependency.writeDependency;

            plan.barrierTransitions.push_back(transition);
            ++stats.transitionCount;
            if (resourceKind == RenderFrameGraphBarrierResourceKind::Buffer) {
                ++stats.bufferTransitionCount;
            } else {
                ++stats.imageTransitionCount;
            }
            if (transition.layoutTransition) {
                ++stats.layoutTransitionCount;
            }
            if (transition.queueOwnershipTransfer) {
                ++stats.queueOwnershipTransferCount;
            }
            if (transition.writeDependency) {
                ++stats.writeAfterWriteTransitionCount;
            } else {
                ++stats.readAfterWriteTransitionCount;
            }
        }
    }

    plan.barriers = stats;
    plan.barrierExecution.plannedBridgeBarrierCount =
        CountBarrierBridgeTransitions(
            plan,
            RenderFrameGraphBarrierBridge::LightTileCullFragmentRead
        ) +
        CountBarrierBridgeTransitions(
            plan,
            RenderFrameGraphBarrierBridge::AutoExposureHistoryFragmentRead
        );
}

void RecordResourceUse(
    RenderGraphResource& resource,
    const RenderFramePass& pass,
    bool writeUse
) {
    if (resource.firstUsePassId == 0u) {
        resource.firstUsePassId = pass.id;
        resource.firstUsePassName = pass.name;
    }
    resource.lastUsePassId = pass.id;
    resource.lastUsePassName = pass.name;
    if (writeUse) {
        ++resource.writeCount;
    } else {
        ++resource.readCount;
    }
}

void RebuildFrameGraphResourceLifetimes(RenderFrameGraphPlan& plan) {
    RenderFrameGraphLifetimeStats stats{};
    for (RenderGraphResource& resource : plan.resources) {
        resource.firstUsePassId = 0u;
        resource.firstUsePassName = {};
        resource.lastUsePassId = 0u;
        resource.lastUsePassName = {};
        resource.readCount = 0u;
        resource.writeCount = 0u;
    }

    for (const RenderFramePass& pass : plan.passes) {
        for (const RenderFrameGraphResourceRef& ref : pass.readResources) {
            RenderGraphResource* resource =
                FindResourceById(plan, ref.resourceId);
            if (resource != nullptr) {
                RecordResourceUse(*resource, pass, false);
            }
        }
        for (const RenderFrameGraphResourceRef& ref : pass.writeResources) {
            RenderGraphResource* resource =
                FindResourceById(plan, ref.resourceId);
            if (resource != nullptr) {
                RecordResourceUse(*resource, pass, true);
            }
        }
    }

    for (const RenderGraphResource& resource : plan.resources) {
        if (resource.readCount == 0u && resource.writeCount == 0u) {
            ++stats.unusedResourceCount;
        } else {
            ++stats.usedResourceCount;
            if (resource.readCount > 0u && resource.writeCount > 0u) {
                ++stats.readWriteResourceCount;
            } else if (resource.readCount > 0u) {
                ++stats.readOnlyResourceCount;
            } else {
                ++stats.writeOnlyResourceCount;
            }
        }
    }
    plan.lifetimes = stats;
}

void RebuildFrameGraphValidation(RenderFrameGraphPlan& plan) {
    RenderFrameGraphValidation validation{};

    std::vector<u32> seenPassIds;
    for (const RenderFramePass& pass : plan.passes) {
        if (pass.name.empty()) {
            AppendValidationIssue(
                validation,
                RenderFrameGraphValidationIssue{
                    RenderFrameGraphValidationIssueKind::UnnamedPass,
                    pass.id,
                    pass.name,
                    0u,
                    {},
                    false
                }
            );
        }
        if (ContainsId(seenPassIds, pass.id)) {
            AppendValidationIssue(
                validation,
                RenderFrameGraphValidationIssue{
                    RenderFrameGraphValidationIssueKind::DuplicatePassId,
                    pass.id,
                    pass.name,
                    0u,
                    {},
                    false
                }
            );
        } else {
            seenPassIds.push_back(pass.id);
        }

        AppendMissingResourceRefIssues(
            plan,
            pass,
            pass.reads,
            false,
            validation
        );
        AppendMissingResourceRefIssues(
            plan,
            pass,
            pass.writes,
            true,
            validation
        );
    }

    std::vector<u32> seenResourceIds;
    for (const RenderGraphResource& resource : plan.resources) {
        if (resource.name.empty()) {
            AppendValidationIssue(
                validation,
                RenderFrameGraphValidationIssue{
                    RenderFrameGraphValidationIssueKind::UnnamedResource,
                    0u,
                    {},
                    resource.id,
                    resource.name,
                    false
                }
            );
        }
        if (ContainsId(seenResourceIds, resource.id)) {
            AppendValidationIssue(
                validation,
                RenderFrameGraphValidationIssue{
                    RenderFrameGraphValidationIssueKind::DuplicateResourceId,
                    0u,
                    {},
                    resource.id,
                    resource.name,
                    false
                }
            );
        } else {
            seenResourceIds.push_back(resource.id);
        }

        if (resource.status == RenderGraphResourceStatus::Physical &&
            resource.readCount == 0u &&
            resource.writeCount == 0u) {
            AppendValidationIssue(
                validation,
                RenderFrameGraphValidationIssue{
                    RenderFrameGraphValidationIssueKind::UnusedPhysicalResource,
                    0u,
                    {},
                    resource.id,
                    resource.name,
                    false
                }
            );
        }

        if (resource.status == RenderGraphResourceStatus::Planned &&
            resource.readCount == 0u &&
            resource.writeCount > 0u &&
            HasRoadmapWriter(plan, resource.id) &&
            !HasActiveWriter(plan, resource.id)) {
            AppendValidationIssue(
                validation,
                RenderFrameGraphValidationIssue{
                    RenderFrameGraphValidationIssueKind::WriteOnlyRoadmapResource,
                    0u,
                    {},
                    resource.id,
                    resource.name,
                    true
                }
            );
        }
    }

    std::vector<u32> activeWrittenResourceIds;
    for (const RenderFramePass& pass : plan.passes) {
        if (pass.status != RenderFramePassStatus::Active) {
            continue;
        }

        for (const RenderFrameGraphResourceRef& ref : pass.readResources) {
            const RenderGraphResource* resource =
                FindResourceById(plan, ref.resourceId);
            if (resource != nullptr &&
                HasActiveWriter(plan, ref.resourceId) &&
                resource->lifetime != RenderGraphResourceLifetime::PersistentHistory &&
                !ContainsId(activeWrittenResourceIds, ref.resourceId)) {
                AppendValidationIssue(
                    validation,
                    RenderFrameGraphValidationIssue{
                        RenderFrameGraphValidationIssueKind::ReadBeforeFirstWrite,
                        pass.id,
                        pass.name,
                        resource->id,
                        resource->name,
                        false
                    }
                );
            }
        }

        for (const RenderFrameGraphResourceRef& ref : pass.writeResources) {
            const RenderGraphResource* resource =
                FindResourceById(plan, ref.resourceId);
            if (resource != nullptr &&
                resource->status == RenderGraphResourceStatus::Planned) {
                AppendValidationIssue(
                    validation,
                    RenderFrameGraphValidationIssue{
                        RenderFrameGraphValidationIssueKind::
                            ActivePassWritesPlannedResource,
                        pass.id,
                        pass.name,
                        resource->id,
                        resource->name,
                        true
                    }
                );
            }
            if (!ContainsId(activeWrittenResourceIds, ref.resourceId)) {
                activeWrittenResourceIds.push_back(ref.resourceId);
            }
        }
    }

    plan.validation = validation;
}

void RebuildFrameGraphResourceReferences(RenderFrameGraphPlan& plan) {
    RenderFrameGraphReferenceStats stats{};
    for (RenderFramePass& pass : plan.passes) {
        pass.readResources.clear();
        pass.writeResources.clear();
        stats.unstructuredReadTokenCount +=
            ResolveResourceRefs(plan, pass.reads, pass.readResources, pass, false);
        stats.unstructuredWriteTokenCount +=
            ResolveResourceRefs(plan, pass.writes, pass.writeResources, pass, true);
        stats.readCount += static_cast<u32>(pass.readResources.size());
        stats.writeCount += static_cast<u32>(pass.writeResources.size());
        for (const RenderFrameGraphResourceRef& ref : pass.readResources) {
            CountAccess(stats, ref.access);
        }
        for (const RenderFrameGraphResourceRef& ref : pass.writeResources) {
            CountAccess(stats, ref.access);
        }
    }
    plan.references = stats;
}

void RebuildFrameGraphDerivedData(RenderFrameGraphPlan& plan) {
    RebuildFrameGraphResourceReferences(plan);
    RebuildFrameGraphPassDependencies(plan);
    RebuildFrameGraphResourceLifetimes(plan);
    RebuildFrameGraphValidation(plan);
    RebuildFrameGraphBarrierPlan(plan);
}

void AppendPass(
    RenderFrameGraphPlan& plan,
    RenderFramePassKind kind,
    RenderFramePassStatus status,
    RenderFramePassQueue queue,
    std::string_view name,
    std::string_view reads,
    std::string_view writes,
    std::string_view purpose
) {
    const u32 id = RenderFramePassStableId(kind, status, queue, name);

    RenderFramePass pass{};
    pass.id = id;
    pass.kind = kind;
    pass.status = status;
    pass.queue = queue;
    pass.name = name;
    pass.reads = reads;
    pass.writes = writes;
    pass.purpose = purpose;
    plan.passes.push_back(pass);
    RebuildFrameGraphDerivedData(plan);

    if (status == RenderFramePassStatus::Active) {
        ++plan.activePassCount;
    } else {
        ++plan.roadmapPassCount;
    }
}

void AppendResource(
    RenderFrameGraphPlan& plan,
    RenderGraphResourceStatus status,
    RenderGraphResourceLifetime lifetime,
    std::string_view name,
    std::string_view format,
    std::string_view usage,
    std::string_view scale
) {
    const u32 id = RenderGraphResourceStableId(status, lifetime, name);

    RenderGraphResource resource{};
    resource.id = id;
    resource.status = status;
    resource.lifetime = lifetime;
    resource.name = name;
    resource.format = format;
    resource.usage = usage;
    resource.scale = scale;
    plan.resources.push_back(resource);
    RebuildFrameGraphDerivedData(plan);

    if (status == RenderGraphResourceStatus::Physical) {
        ++plan.physicalResourceCount;
    } else {
        ++plan.plannedResourceCount;
    }
}

std::string_view VulkanFormatName(VkFormat format) {
    switch (format) {
    case VK_FORMAT_UNDEFINED:
        return "undefined";
    case VK_FORMAT_B8G8R8A8_SRGB:
        return "B8G8R8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:
        return "R8G8B8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return "R16G16B16A16_SFLOAT";
    case VK_FORMAT_R16_SFLOAT:
        return "R16_SFLOAT";
    case VK_FORMAT_R16G16_SFLOAT:
        return "R16G16_SFLOAT";
    case VK_FORMAT_R16G16B16A16_UNORM:
        return "R16G16B16A16_UNORM";
    case VK_FORMAT_R32_UINT:
        return "R32_UINT";
    case VK_FORMAT_D32_SFLOAT:
        return "D32_SFLOAT";
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return "D32_SFLOAT_S8_UINT";
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return "D24_UNORM_S8_UINT";
    default:
        return "VkFormat";
    }
}

void AppendAAARoadmapPasses(RenderFrameGraphPlan& plan) {
    AppendPass(
        plan,
        RenderFramePassKind::FrameSetup,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "FrameSetup",
        "",
        "",
        "Own per-frame state before any render pass records work."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Visibility,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Compute,
        "GPUVisibility",
        "SceneDepth",
        "SceneDepth",
        "Move large-scene culling and draw preparation toward GPU-driven rendering."
    );
    AppendPass(
        plan,
        RenderFramePassKind::VirtualGeometry,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Compute,
        "VirtualGeometryClusters",
        "VirtualGeometryClusters",
        "VirtualGeometryClusters",
        "Nanite-like cluster LOD, culling, streaming, and visibility output."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Shadow,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "VirtualShadowMaps",
        "VirtualShadowPhysicalPages",
        "VirtualShadowPhysicalPages",
        "VSM clipmaps and local-light pages with cache invalidation."
    );
    AppendPass(
        plan,
        RenderFramePassKind::DepthPrepass,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "DepthVelocity",
        "",
        "SceneDepth, Velocity",
        "Feed TAA/TSR, occlusion, motion blur, and screen-space tracing."
    );
    AppendPass(
        plan,
        RenderFramePassKind::GBuffer,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "GBuffer",
        "SceneDepth",
        "GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive",
        "Default opaque path for deferred PBR lighting."
    );
    AppendPass(
        plan,
        RenderFramePassKind::DeferredLighting,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "DeferredLighting",
        "GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive, SceneDepth, VirtualShadowPhysicalPages",
        "HDRSceneColor",
        "Direct PBR lighting with tiled or clustered light lists."
    );
    AppendPass(
        plan,
        RenderFramePassKind::GlobalIllumination,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::AsyncCompute,
        "DynamicGI",
        "SurfaceCacheCards, SceneDepth, GBufferNormalRoughness",
        "TemporalHistory",
        "Lumen-like software and hardware trace paths with temporal denoising."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Reflections,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::AsyncCompute,
        "Reflections",
        "HDRSceneColor, SceneDepth, GBufferNormalRoughness, SurfaceCacheCards, TemporalHistory",
        "TemporalHistory",
        "Screen, probe, software trace, and hardware ray tracing reflection tiers."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Forward,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "ForwardPlus",
        "HDRSceneColor, SceneDepth",
        "HDRSceneColor",
        "Transparent, special, particle, black-hole, and unsupported deferred materials."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Volumetrics,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "VolumetricsAtmosphere",
        "HDRSceneColor, VirtualShadowPhysicalPages",
        "HDRSceneColor",
        "Fog, clouds, light shafts, and volumetric shadows."
    );
    AppendPass(
        plan,
        RenderFramePassKind::PostProcess,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "PostProcess",
        "HDRSceneColor",
        "HDRSceneColor",
        "Auto exposure, bloom, color grading, DOF, motion blur, and lens effects."
    );
    AppendPass(
        plan,
        RenderFramePassKind::TemporalUpscale,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::AsyncCompute,
        "TemporalUpscale",
        "HDRSceneColor, SceneDepth, Velocity, TemporalHistory",
        "HDRSceneColor, TemporalHistory",
        "TSR/FSR-style dynamic-resolution upscaling and anti-aliasing."
    );
}

void AppendAAAResourceBlueprint(
    RenderFrameGraphPlan& plan,
    bool includeHdrSceneColor = true,
    bool includeDeferredTargets = true,
    bool includeWeightedTranslucencyTargets = true
) {
    if (includeHdrSceneColor) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "HDRSceneColor",
            "R16G16B16A16_SFLOAT",
            "color attachment, sampled, storage",
            "dynamic internal resolution"
        );
    }
    if (includeDeferredTargets) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "SceneDepth",
            "D32_SFLOAT",
            "depth attachment, sampled, Hi-Z source",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "Velocity",
            "R16G16_SFLOAT",
            "color attachment, sampled",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferAlbedo",
            "R8G8B8A8_SRGB",
            "color attachment, sampled",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferNormalRoughness",
            "R16G16B16A16_SFLOAT",
            "color attachment, sampled",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferMaterial",
            "R8G8B8A8_UNORM",
            "color attachment, sampled",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferEmissive",
            "R16G16B16A16_SFLOAT",
            "color attachment, sampled",
            "dynamic internal resolution"
        );
    }
    if (includeWeightedTranslucencyTargets) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "WeightedTranslucencyAccum",
            "R16G16B16A16_SFLOAT",
            "color attachment, sampled, storage",
            "dynamic internal resolution"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "WeightedTranslucencyRevealage",
            "R16_SFLOAT",
            "color attachment, sampled, storage",
            "dynamic internal resolution"
        );
    }
    AppendResource(
        plan,
        RenderGraphResourceStatus::Planned,
        RenderGraphResourceLifetime::PersistentHistory,
        "TemporalHistory",
        "R16G16B16A16_SFLOAT",
        "sampled, storage",
        "display resolution"
    );
    AppendResource(
        plan,
        RenderGraphResourceStatus::Planned,
        RenderGraphResourceLifetime::PersistentCache,
        "VirtualShadowPhysicalPages",
        "D32_SFLOAT",
        "depth attachment, sampled",
        "page atlas"
    );
    AppendResource(
        plan,
        RenderGraphResourceStatus::Planned,
        RenderGraphResourceLifetime::PersistentCache,
        "SurfaceCacheCards",
        "runtime selected",
        "sampled, storage",
        "card atlas"
    );
    AppendResource(
        plan,
        RenderGraphResourceStatus::Planned,
        RenderGraphResourceLifetime::PersistentCache,
        "VirtualGeometryClusters",
        "structured buffers",
        "storage, indirect args",
        "streaming cache"
    );
}

} // namespace

std::string_view RenderFramePassStatusName(RenderFramePassStatus status) {
    switch (status) {
    case RenderFramePassStatus::Active:
        return "active";
    case RenderFramePassStatus::Roadmap:
        return "roadmap";
    }

    return "unknown";
}

std::string_view RenderFramePassQueueName(RenderFramePassQueue queue) {
    switch (queue) {
    case RenderFramePassQueue::Graphics:
        return "graphics";
    case RenderFramePassQueue::Compute:
        return "compute";
    case RenderFramePassQueue::AsyncCompute:
        return "async compute";
    case RenderFramePassQueue::Transfer:
        return "transfer";
    case RenderFramePassQueue::Present:
        return "present";
    }

    return "unknown";
}

std::string_view RenderGraphResourceStatusName(RenderGraphResourceStatus status) {
    switch (status) {
    case RenderGraphResourceStatus::Physical:
        return "physical";
    case RenderGraphResourceStatus::Planned:
        return "planned";
    }

    return "unknown";
}

std::string_view RenderGraphResourceLifetimeName(RenderGraphResourceLifetime lifetime) {
    switch (lifetime) {
    case RenderGraphResourceLifetime::Swapchain:
        return "swapchain";
    case RenderGraphResourceLifetime::PerFrame:
        return "per frame";
    case RenderGraphResourceLifetime::PersistentHistory:
        return "history";
    case RenderGraphResourceLifetime::PersistentCache:
        return "cache";
    }

    return "unknown";
}

std::string_view RenderFrameGraphResourceAccessName(
    RenderFrameGraphResourceAccess access
) {
    switch (access) {
    case RenderFrameGraphResourceAccess::ReadSampled:
        return "read sampled";
    case RenderFrameGraphResourceAccess::ReadAttachment:
        return "read attachment";
    case RenderFrameGraphResourceAccess::WriteColorAttachment:
        return "write color";
    case RenderFrameGraphResourceAccess::WriteDepthAttachment:
        return "write depth";
    case RenderFrameGraphResourceAccess::WriteStorage:
        return "write storage";
    case RenderFrameGraphResourceAccess::Present:
        return "present";
    }

    return "unknown";
}

std::string_view RenderFrameGraphValidationIssueKindName(
    RenderFrameGraphValidationIssueKind kind
) {
    switch (kind) {
    case RenderFrameGraphValidationIssueKind::UnnamedPass:
        return "unnamed pass";
    case RenderFrameGraphValidationIssueKind::DuplicatePassId:
        return "duplicate pass id";
    case RenderFrameGraphValidationIssueKind::UnnamedResource:
        return "unnamed resource";
    case RenderFrameGraphValidationIssueKind::DuplicateResourceId:
        return "duplicate resource id";
    case RenderFrameGraphValidationIssueKind::MissingResourceRef:
        return "missing resource ref";
    case RenderFrameGraphValidationIssueKind::ReadBeforeFirstWrite:
        return "read before first write";
    case RenderFrameGraphValidationIssueKind::UnusedPhysicalResource:
        return "unused physical resource";
    case RenderFrameGraphValidationIssueKind::WriteOnlyRoadmapResource:
        return "write-only roadmap resource";
    case RenderFrameGraphValidationIssueKind::ActivePassWritesPlannedResource:
        return "active pass writes planned resource";
    }

    return "unknown";
}

std::string_view RenderFrameGraphBarrierResourceKindName(
    RenderFrameGraphBarrierResourceKind kind
) {
    switch (kind) {
    case RenderFrameGraphBarrierResourceKind::Image:
        return "image";
    case RenderFrameGraphBarrierResourceKind::Buffer:
        return "buffer";
    }

    return "unknown";
}

std::string_view RenderFrameGraphBarrierBridgeName(
    RenderFrameGraphBarrierBridge bridge
) {
    switch (bridge) {
    case RenderFrameGraphBarrierBridge::LightTileCullFragmentRead:
        return "LightTileCull fragment read";
    case RenderFrameGraphBarrierBridge::AutoExposureHistoryFragmentRead:
        return "AutoExposureHistory fragment read";
    }

    return "unknown";
}

RenderFrameGraphBarrierExecutionResult RecordRenderFrameGraphBarrierExecution(
    RenderFrameGraphPlan& plan,
    RenderFrameGraphBarrierBridge bridge
) {
    RenderFrameGraphBarrierExecutionResult result{};
    result.plannedBarrierCount = CountBarrierBridgeTransitions(plan, bridge);
    ++plan.barrierExecution.executedBarrierCount;

    if (result.plannedBarrierCount > 0u) {
        result.matched = true;
    } else {
        result.fallback = true;
        result.mismatch = true;
        ++plan.barrierExecution.fallbackBarrierCount;
        ++plan.barrierExecution.mismatchCount;
    }

    plan.barrierExecution.plannedBridgeBarrierCount =
        CountBarrierBridgeTransitions(
            plan,
            RenderFrameGraphBarrierBridge::LightTileCullFragmentRead
        ) +
        CountBarrierBridgeTransitions(
            plan,
            RenderFrameGraphBarrierBridge::AutoExposureHistoryFragmentRead
        );
    return result;
}

RenderFrameGraphPlan BuildCurrentVulkanFrameGraphPlan(
    CurrentVulkanFrameGraphInputs inputs
) {
    RenderFrameGraphPlan plan{};
    plan.name = "SelfEngine Hybrid Renderer";
    plan.target = "UE5-class AAA frame graph";

    AppendResource(
        plan,
        RenderGraphResourceStatus::Physical,
        RenderGraphResourceLifetime::Swapchain,
        "SwapchainColor",
        VulkanFormatName(inputs.swapchainFormat),
        "present color attachment",
        inputs.extent.width > 0 && inputs.extent.height > 0
            ? "window extent"
            : "unknown extent"
    );
    AppendResource(
        plan,
        RenderGraphResourceStatus::Physical,
        RenderGraphResourceLifetime::PerFrame,
        "LegacyDepth",
        VulkanFormatName(inputs.depthFormat),
        "depth attachment",
        "window extent"
    );
    if (inputs.shadowMapSize > 0) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "LegacyShadowMap",
            "D32_SFLOAT",
            "sampled depth attachment",
            "fixed square"
        );
    }
    if (inputs.directionalShadowAtlasWidth > 0 &&
        inputs.directionalShadowAtlasHeight > 0 &&
        inputs.directionalShadowAtlasTileSize > 0) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "DirectionalShadowCascades",
            "depth atlas",
            "sampled depth attachment, cascade tiles",
            "shadow tile grid"
        );
    } else if (inputs.directionalShadowCascadeScaffoldEnabled &&
        inputs.directionalShadowCascadeCount > 0) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Planned,
            RenderGraphResourceLifetime::PerFrame,
            "DirectionalShadowCascades",
            "CPU matrices + split depths",
            "shadow cascade selection metadata",
            "camera split count"
        );
    }
    if (inputs.localShadowAtlasWidth > 0 &&
        inputs.localShadowAtlasHeight > 0 &&
        inputs.localShadowAtlasTileSize > 0) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "LocalShadowAtlas",
            "depth atlas",
            "sampled depth attachment, point/spot light tiles",
            "shadow tile grid"
        );
    }
    if (inputs.hdrSceneColorAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "HDRSceneColor",
            VulkanFormatName(inputs.hdrSceneColorFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
    }
    if (inputs.bloomPyramidAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "BloomPyramid",
            VulkanFormatName(inputs.bloomPyramidFormat),
            "sampled color attachments, downsample/upsample chain",
            inputs.bloomPyramidMipCount > 0 ? "half-res mip chain" : "unknown mips"
        );
    }
    if (inputs.colorGradingLutAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PersistentHistory,
            "ColorGradingLUT",
            VulkanFormatName(inputs.colorGradingLutFormat),
            "sampled neutral 2D LUT strip",
            inputs.colorGradingLutSize > 0 ? "renderer-owned LUT" : "unknown LUT"
        );
    }
    if (inputs.iblBrdfLutAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PersistentCache,
            "BRDFLUT",
            VulkanFormatName(inputs.iblBrdfLutFormat),
            "sampled split-sum GGX BRDF integration LUT",
            inputs.iblBrdfLutSize > 0 ? "renderer-owned LUT" : "unknown LUT"
        );
    }
    if (inputs.iblIrradianceMapAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PersistentCache,
            "IrradianceMap",
            VulkanFormatName(inputs.iblIrradianceFormat),
            "sampled diffuse irradiance cubemap",
            inputs.iblIrradianceFaceSize > 0 ? "cube face cache" : "unknown cube"
        );
    }
    if (inputs.iblPrefilteredMapAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PersistentCache,
            "PrefilteredEnvironmentMap",
            VulkanFormatName(inputs.iblPrefilteredFormat),
            "sampled prefiltered specular environment cubemap",
            inputs.iblPrefilteredMipCount > 0 ? "mipped cube cache" : "unknown cube"
        );
    }
    if (inputs.sceneReflectionProbesAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "SceneReflectionProbes",
            "frame UBO payload",
            "scene-owned reflection-probe influence data",
            inputs.sceneReflectionProbeCount > 0 ? "active scene probes" : "empty probe set"
        );
    }
    if (inputs.reflectionCaptureSourceAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PersistentCache,
            "ReflectionCaptureSource",
            "source policy",
            "reflection capture source and fallback diagnostics",
            inputs.reflectionCaptureFallbackReason == 0u
                ? "resource-ready source"
                : "fallback-selected source"
        );
    }
    if (inputs.sceneReflectionProbeCubemapAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PersistentCache,
            "SceneReflectionProbeCubemap",
            VulkanFormatName(inputs.sceneReflectionProbeCubemapFormat),
            "sampled local reflection-probe cubemap resolved from ReflectionCaptureSource",
            inputs.sceneReflectionProbeCubemapMipCount > 0
                ? "mipped cube cache"
                : "unknown cube"
        );
    }
    if (inputs.autoExposureHistogramEnabled) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "AutoExposureHistogram",
            "64-bin structured buffer",
            "storage buffer, compute-written luminance histogram",
            "per frame"
        );
    }
    if (inputs.autoExposureHistoryAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PersistentHistory,
            "AutoExposureHistory",
            "structured buffer",
            "storage buffer, exposure history and resolved exposure",
            "swapchain history"
        );
    }
    if (inputs.weightedTranslucencyTargetsAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "WeightedTranslucencyAccum",
            VulkanFormatName(inputs.weightedTranslucencyAccumFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "WeightedTranslucencyRevealage",
            VulkanFormatName(inputs.weightedTranslucencyRevealageFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
    }
    if (inputs.deferredTargetsAllocated) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "SceneDepth",
            VulkanFormatName(inputs.sceneDepthFormat),
            "depth attachment, sampled, Hi-Z source",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "Velocity",
            VulkanFormatName(inputs.velocityFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferAlbedo",
            VulkanFormatName(inputs.gBufferAlbedoFormat),
            "color attachment, sampled",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferNormalRoughness",
            VulkanFormatName(inputs.gBufferNormalRoughnessFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferMaterial",
            VulkanFormatName(inputs.gBufferMaterialFormat),
            "color attachment, sampled",
            "window extent"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "GBufferEmissive",
            VulkanFormatName(inputs.gBufferEmissiveFormat),
            "color attachment, sampled, storage",
            "window extent"
        );
    }
    if (inputs.lightTileCullComputeEnabled) {
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "LightTileLists",
            "structured buffer",
            "storage buffer, compute-written tile metadata and light index lists",
            "tile grid"
        );
        AppendResource(
            plan,
            RenderGraphResourceStatus::Physical,
            RenderGraphResourceLifetime::PerFrame,
            "LightTileDiagnostics",
            "structured buffer",
            "storage buffer, compute-written tile cull counters",
            "per frame"
        );
    }
    if (inputs.gBufferRenderPassAllocated) {
        AppendPass(
            plan,
            RenderFramePassKind::GBuffer,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            inputs.gBufferGeometryEnabled ? "GBufferOpaque" : "GBufferTarget",
            "",
            "SceneDepth, Velocity, GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive",
            inputs.gBufferGeometryEnabled
                ? "Writes the first deferred opaque material data while legacy forward still owns the visible image."
                : "Recorded clear-only GBuffer pass; opaque geometry migration follows."
        );
    }
    if (inputs.shadowPassEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::Shadow,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "LegacyShadowDepth",
            "",
            "LegacyShadowMap",
            "Current directional shadow-map path kept as the fallback tier."
        );
    }
    if (inputs.directionalShadowCascadeScaffoldEnabled &&
        inputs.directionalShadowCascadeCount > 1) {
        AppendPass(
            plan,
            RenderFramePassKind::Shadow,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "DirectionalCSMScaffold",
            "",
            inputs.directionalShadowAtlasPasses > 0
                ? "DirectionalShadowCascades"
                : "",
            inputs.directionalShadowAtlasPasses > 0
                ? "Records one shadow-depth tile pass per active directional cascade while the single shadow map remains the sampled fallback."
                : "CSM split and stable texel diagnostics feeding the current single-map fallback."
        );
    }
    if (inputs.localShadowAtlasWidth > 0 &&
        inputs.localShadowAtlasHeight > 0 &&
        inputs.localShadowAtlasTileSize > 0) {
        AppendPass(
            plan,
            RenderFramePassKind::Shadow,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "LocalShadowAtlasBudget",
            "",
            inputs.localShadowAtlasAssignedTiles > 0
                ? "LocalShadowAtlas"
                : "",
            "Physical atlas resource and occupancy diagnostics for upcoming point/spot shadow rendering."
        );
    }
    if (inputs.lightTileCullComputeEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::DeferredLighting,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Compute,
            "LightTileCull",
            "",
            "LightTileLists, LightTileDiagnostics",
            "First compute-backed tiled light-list write feeding deferred lighting."
        );
    }
    if (inputs.weightedTranslucencyRenderPassAllocated &&
        inputs.weightedTranslucencyFramebufferCount > 0) {
        AppendPass(
            plan,
            RenderFramePassKind::Forward,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "WeightedTranslucencyForwardPlus",
            inputs.lightTileCullComputeEnabled
                ? "SceneDepth, LightTileLists, BRDFLUT, IrradianceMap, PrefilteredEnvironmentMap"
                : "SceneDepth, BRDFLUT, IrradianceMap, PrefilteredEnvironmentMap",
            "WeightedTranslucencyAccum, WeightedTranslucencyRevealage",
            "Clears and writes weighted blended translucency accum/revealage targets after tiled light culling."
        );
    }
    if (inputs.hdrRenderPassAllocated) {
        AppendPass(
            plan,
            inputs.deferredLightingEnabled
                ? RenderFramePassKind::DeferredLighting
                : RenderFramePassKind::PostProcess,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            inputs.deferredLightingEnabled
                ? "DeferredLighting"
                : "HdrOffscreenTarget",
            inputs.deferredLightingEnabled
                ? (inputs.lightTileCullComputeEnabled
                    ? "GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive, SceneDepth, LightTileLists, BRDFLUT, IrradianceMap, PrefilteredEnvironmentMap"
                    : "GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive, SceneDepth, BRDFLUT, IrradianceMap, PrefilteredEnvironmentMap")
                : "",
            "HDRSceneColor",
            inputs.deferredLightingEnabled
                ? "Fullscreen lighting pass consumes the first GBuffer and writes HDR scene color while legacy forward remains the visible reference."
                : "Recorded offscreen HDR clear pass; scene rendering migrates into this target in the next slice."
        );
    }
    if (inputs.appendRenderFeatures != nullptr) {
        inputs.appendRenderFeatures(
            plan,
            RenderFramePassKind::DeferredLighting,
            inputs.appendRenderFeaturesUserData
        );
    }
    if (inputs.weightedTranslucencyRenderPassAllocated &&
        inputs.weightedTranslucencyFramebufferCount > 0) {
        AppendPass(
            plan,
            RenderFramePassKind::Forward,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "WeightedTranslucencyResolve",
            "HDRSceneColor, WeightedTranslucencyAccum, WeightedTranslucencyRevealage",
            "HDRSceneColor",
            "Resolves weighted blended translucency accum/revealage back into HDR scene color."
        );
    }

    if (inputs.appendRenderFeatures != nullptr) {
        inputs.appendRenderFeatures(
            plan,
            RenderFramePassKind::PostProcess,
            inputs.appendRenderFeaturesUserData
        );
    }

    if (inputs.gBufferDebugEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::PostProcess,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "GBufferDebug",
            "GBufferAlbedo, GBufferNormalRoughness, GBufferMaterial, GBufferEmissive, SceneDepth, Velocity, LegacyShadowMap",
            "SwapchainColor",
            "Debug visualizer for deferred attachments and reconstructed deferred shadow visibility."
        );
    }

    AppendPass(
        plan,
        inputs.usesLegacyForwardMain
            ? RenderFramePassKind::Forward
            : RenderFramePassKind::GBuffer,
        RenderFramePassStatus::Active,
        RenderFramePassQueue::Graphics,
        inputs.has3DMainPass ? "LegacyForward3D" : "Legacy2D",
        inputs.has3DMainPass
            ? (inputs.lightTileCullComputeEnabled
                ? "LightTileLists, BRDFLUT, IrradianceMap, PrefilteredEnvironmentMap"
                : "BRDFLUT, IrradianceMap, PrefilteredEnvironmentMap")
            : "",
        "SwapchainColor, LegacyDepth",
        "Current compatibility path while HDR/deferred targets are introduced."
    );

    if (inputs.overlayPassEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::Forward,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "OverlayForward3D",
            "SwapchainColor, LegacyDepth",
            "SwapchainColor, LegacyDepth",
            "Current secondary 3D path used by the black-hole demo."
        );
    }

    if (inputs.imguiPassEnabled) {
        AppendPass(
            plan,
            RenderFramePassKind::UserInterface,
            RenderFramePassStatus::Active,
            RenderFramePassQueue::Graphics,
            "ImGui",
            "SwapchainColor",
            "SwapchainColor",
            "Runtime controls, pass visibility, and performance diagnostics."
        );
    }

    AppendPass(
        plan,
        RenderFramePassKind::Present,
        RenderFramePassStatus::Active,
        RenderFramePassQueue::Present,
        "Present",
        "SwapchainColor",
        "",
        "Final swapchain present."
    );

    AppendAAARoadmapPasses(plan);
    AppendAAAResourceBlueprint(
        plan,
        !inputs.hdrSceneColorAllocated,
        !inputs.deferredTargetsAllocated,
        !inputs.weightedTranslucencyTargetsAllocated
    );
    return plan;
}

RenderFrameGraphPlan BuildAAAFrameGraphBlueprint() {
    RenderFrameGraphPlan plan{};
    plan.name = "SelfEngine AAA Blueprint";
    plan.target = "Mainstream AAA hybrid renderer";
    AppendAAARoadmapPasses(plan);
    AppendAAAResourceBlueprint(plan);
    AppendPass(
        plan,
        RenderFramePassKind::UserInterface,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Graphics,
        "DebugUI",
        "",
        "",
        "Editor and runtime inspection over the graph output."
    );
    AppendPass(
        plan,
        RenderFramePassKind::Present,
        RenderFramePassStatus::Roadmap,
        RenderFramePassQueue::Present,
        "Present",
        "",
        "",
        "Presentation after UI composition."
    );
    return plan;
}

void AppendRenderFrameGraphPass(
    RenderFrameGraphPlan& plan,
    RenderFramePassKind kind,
    RenderFramePassStatus status,
    RenderFramePassQueue queue,
    std::string_view name,
    std::string_view reads,
    std::string_view writes,
    std::string_view purpose
) {
    AppendPass(
        plan,
        kind,
        status,
        queue,
        name,
        reads,
        writes,
        purpose
    );
}

}
