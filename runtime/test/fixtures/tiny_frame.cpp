// A stand-in title for `gta6rt --run` that reaches the D3D12 layer the way a title does: it imports
// d3d12_xs.dll!D3D12XboxCreateDevice (from a link-time stub; the shim registry answers it) and then does the least
// a title does before its first frame, on the device that call returns: a direct queue, an allocator, a command
// list, a fence, an RTV heap whose start handle is read in the console shape (the 8-byte handle in RAX, heap slot 9),
// a committed render target, a clear, a copy into a readback buffer, Signal and a fence wait, Map, and a check of
// every pixel. It then returns from main with the device, the queue and the fence still alive, as a title's exit
// does. With LEONIDA_D3D12=vulkan (check_tiny_frame.py sets it) that device is the Vulkan-backed one, so this is the
// loader, the game thread, the Xbox-ABI proxies and the Vulkan device in one process. Hand-written test code, public
// D3D12 API only; no game data and no shader.
#include <windows.h>
#include <d3d12.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" __declspec(dllimport) HRESULT __stdcall D3D12XboxCreateDevice(IUnknown* adapter, const void* params,
                                                                        const IID* riid, void** device);

namespace {

int g_failures = 0;

bool check(bool ok, const char* what) {
    std::printf("tiny_frame: %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
    return ok;
}

int finish() {
    std::printf("tiny_frame: %s\n", g_failures ? "FAILED" : "OK");
    std::fflush(stdout);
    return g_failures ? 1 : 0;
}

// Descriptor-heap slot 9 on the console returns the handle in RAX (docs/d3d12_vulkan.md section 7); the PC header's
// GetCPUDescriptorHandleForHeapStart returns it through a hidden pointer, which the heap proxy does not serve.
using HeapStartFn = std::uint64_t(__stdcall*)(void* heap);

constexpr UINT kWidth = 8, kHeight = 4;

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // The entry point's second argument is a block the shim logs and does not interpret; a title passes its own.
    std::uint32_t params[24] = {};
    const IID device_iid = __uuidof(ID3D12Device);
    ID3D12Device* device = nullptr;
    HRESULT hr = D3D12XboxCreateDevice(nullptr, params, &device_iid, reinterpret_cast<void**>(&device));
    if (!check(hr == S_OK && device, "D3D12XboxCreateDevice returns a device")) {
        std::printf("tiny_frame: hr=0x%08lx\n", static_cast<unsigned long>(hr));
        return finish();
    }

    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    check(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) && queue, "a direct command queue");
    check(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) &&
              allocator, "a command allocator");
    if (allocator)
        check(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                                  IID_PPV_ARGS(&list))) && list, "a command list");
    check(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) && fence, "a fence");

    ID3D12DescriptorHeap* rtv_heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap_desc.NumDescriptors = 1;
    check(SUCCEEDED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap))) && rtv_heap, "an RTV heap");
    if (!queue || !list || !fence || !rtv_heap) return finish();
    const std::uint64_t start = reinterpret_cast<HeapStartFn>((*reinterpret_cast<void***>(rtv_heap))[9])(rtv_heap);
    check(start != 0, "the heap's start handle comes back in RAX (heap slot 9)");
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv{static_cast<SIZE_T>(start)};

    D3D12_HEAP_PROPERTIES default_heap{}, readback_heap{};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC target_desc{};
    target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    target_desc.Width = kWidth;
    target_desc.Height = kHeight;
    target_desc.DepthOrArraySize = 1;
    target_desc.MipLevels = 1;
    target_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    target_desc.SampleDesc.Count = 1;
    target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ID3D12Resource* target = nullptr;
    check(SUCCEEDED(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &target_desc,
                                                    D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                    IID_PPV_ARGS(&target))) && target, "a committed render target");

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed{};
    UINT rows = 0;
    UINT64 row_bytes = 0, total = 0;
    device->GetCopyableFootprints(&target_desc, 0, 1, 0, &placed, &rows, &row_bytes, &total);
    const UINT64 footprint_bytes = UINT64(placed.Footprint.RowPitch) * (kHeight - 1) + kWidth * 4;
    if (!check(rows == kHeight && row_bytes == kWidth * 4 && placed.Footprint.RowPitch >= kWidth * 4 &&
                   placed.Offset <= total && footprint_bytes <= total - placed.Offset,
               "GetCopyableFootprints describes the target within the readback buffer"))
        return finish();
    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = total + sizeof(std::uint32_t);
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* readback = nullptr;
    check(SUCCEEDED(device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(&readback))) && readback, "a readback buffer");
    if (!target || !readback) return finish();

    device->CreateRenderTargetView(target, nullptr, rtv);
    const FLOAT colour[4] = {64.0f / 255.0f, 128.0f / 255.0f, 191.0f / 255.0f, 1.0f};
    list->ClearRenderTargetView(rtv, colour, 0, nullptr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION from{}, to{};
    from.pResource = target;
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = readback;
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint = placed;
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    check(SUCCEEDED(list->Close()), "the list closes");
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);

    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const bool signalled = check(SUCCEEDED(queue->Signal(fence, 1)), "Signal");
    const bool finished = signalled &&
        check(done && SUCCEEDED(fence->SetEventOnCompletion(1, done)) &&
                  WaitForSingleObject(done, 30000) == WAIT_OBJECT_0,
              "the fence reaches the signalled value");
    if (done) CloseHandle(done);
    if (!finished) return finish();
    const UINT64 completed = fence->GetCompletedValue();
    if (!check(completed >= 1 && completed != UINT64_MAX, "GetCompletedValue confirms GPU completion"))
        return finish();

    void* bytes = nullptr;
    if (check(SUCCEEDED(readback->Map(0, nullptr, &bytes)) && bytes, "Map of the readback buffer")) {
        const std::uint8_t want[4] = {64, 128, 191, 255};
        unsigned wrong = 0;
        for (UINT y = 0; y < kHeight; ++y)
            for (UINT x = 0; x < kWidth; ++x)
                wrong += std::memcmp(static_cast<const std::uint8_t*>(bytes) + placed.Offset +
                                         y * placed.Footprint.RowPitch + x * 4, want, 4) != 0;
        check(wrong == 0, "all 32 pixels are the clear colour (64, 128, 191, 255)");
        readback->Unmap(0, nullptr);
    }

    bytes = nullptr;
    if (!check(SUCCEEDED(readback->Map(0, nullptr, &bytes)) && bytes, "Map to clear prior readback"))
        return finish();
    std::memset(bytes, 0, static_cast<std::size_t>(total));
    readback->Unmap(0, nullptr);
    list->Release();  // this allocator is now free to record another list
    list = nullptr;
    const IID xbox_list_iid{0xc3827890, 0x4024, 0x474c, {0x98, 0xcf, 0x64, 0x5b, 0x51, 0xdc, 0x48, 0x74}};
    using CreateXboxList = HRESULT(WINAPI*)(ID3D12Device*, const void*, ID3D12CommandAllocator*,
                                            ID3D12PipelineState*, const IID*, void**);
    const auto create_xbox = reinterpret_cast<CreateXboxList>((*reinterpret_cast<void***>(device))[73]);
    const std::uint32_t xbox_desc[4]{D3D12_COMMAND_LIST_TYPE_DIRECT, 0, 0, 0};
    void* xbox_list = nullptr;
    if (!check(SUCCEEDED(create_xbox(device, xbox_desc, allocator, nullptr, &xbox_list_iid, &xbox_list)) &&
                   xbox_list, "an Xbox command list"))
        return finish();

    D3D12_HEAP_PROPERTIES upload_heap{};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    ID3D12Resource* upload = nullptr;
    if (!check(SUCCEEDED(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                                         D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                         IID_PPV_ARGS(&upload))) && upload, "an upload buffer"))
        return finish();
    const std::uint32_t pattern = 0x173ac95e;
    bytes = nullptr;
    if (!check(SUCCEEDED(upload->Map(0, nullptr, &bytes)) && bytes, "Map of the upload buffer"))
        return finish();
    std::memcpy(bytes, &pattern, sizeof(pattern));
    upload->Unmap(0, nullptr);

    auto** xbox_table = *static_cast<void***>(xbox_list);
    using AllocateXboxUpload = void*(WINAPI*)(void*, UINT64, UINT64);
    const auto allocate_upload = reinterpret_cast<AllocateXboxUpload>(xbox_table[148]);
    check(allocate_upload(xbox_list, UINT64_MAX, 256) == nullptr, "Xbox upload rejects size overflow");
    auto* upload_address = allocate_upload(xbox_list, sizeof(pattern), 256);
    if (check(upload_address && (reinterpret_cast<std::uintptr_t>(upload_address) & 255) == 0,
              "Xbox upload returns aligned mapped memory")) {
        std::memcpy(upload_address, &pattern, sizeof(pattern));
        const D3D12_VERTEX_BUFFER_VIEW vertex{reinterpret_cast<UINT64>(upload_address),
                                             sizeof(pattern), sizeof(pattern)};
        reinterpret_cast<void(WINAPI*)(void*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*)>(xbox_table[48])(
            xbox_list, 0, 1, &vertex);
    }
    D3D12_GLOBAL_BARRIER global{D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_COPY,
                                D3D12_BARRIER_ACCESS_COPY_DEST, D3D12_BARRIER_ACCESS_COPY_DEST};
    D3D12_BARRIER_GROUP group{D3D12_BARRIER_TYPE_GLOBAL, 1};
    group.pGlobalBarriers = &global;
    reinterpret_cast<void(WINAPI*)(void*, const D3D12_BARRIER_GROUP*)>(xbox_table[146])(xbox_list, &group);
    reinterpret_cast<void(WINAPI*)(void*, const D3D12_TEXTURE_COPY_LOCATION*, UINT, UINT, UINT,
                                    const D3D12_TEXTURE_COPY_LOCATION*, const D3D12_BOX*)>(xbox_table[16])(
        xbox_list, &to, 0, 0, 0, &from, nullptr);
    reinterpret_cast<void(WINAPI*)(void*, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64, UINT64)>(
        xbox_table[15])(xbox_list, readback, total, upload, 0, sizeof(pattern));
    if (!check(SUCCEEDED(reinterpret_cast<HRESULT(WINAPI*)(void*)>(xbox_table[9])(xbox_list)),
               "the Xbox list closes"))
        return finish();
    ID3D12Fence* gate = nullptr;
    if (!check(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate))) && gate,
               "a fence to defer Xbox list submission"))
        return finish();
    if (!check(SUCCEEDED(queue->Wait(gate, 1)), "the Xbox queue waits before replay")) return finish();
    ID3D12CommandList* xbox_lists[]{reinterpret_cast<ID3D12CommandList*>(xbox_list)};
    queue->ExecuteCommandLists(1, xbox_lists);
    reinterpret_cast<IUnknown*>(xbox_list)->Release();

    done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const bool queued_signal = check(SUCCEEDED(queue->Signal(fence, 2)) && fence->GetCompletedValue() == 1,
                                     "the deferred Xbox list has not completed before opening the gate");
    const bool opened_gate = check(SUCCEEDED(gate->Signal(1)), "open the gate after releasing the Xbox list");
    gate->Release();
    const bool xbox_finished = queued_signal && opened_gate && done &&
                               SUCCEEDED(fence->SetEventOnCompletion(2, done)) &&
                               WaitForSingleObject(done, 30000) == WAIT_OBJECT_0;
    if (done) CloseHandle(done);
    if (!check(xbox_finished, "Xbox list fence reaches completion")) return finish();
    bytes = nullptr;
    if (check(SUCCEEDED(readback->Map(0, nullptr, &bytes)) && bytes, "Map after Xbox list submission")) {
        const auto* output = static_cast<const std::uint8_t*>(bytes);
        check(std::memcmp(output + total, &pattern, sizeof(pattern)) == 0, "Xbox list copy writes the expected bytes");
        const std::uint8_t want[4] = {64, 128, 191, 255};
        unsigned wrong = 0;
        for (UINT y = 0; y < kHeight; ++y)
            for (UINT x = 0; x < kWidth; ++x)
                wrong += std::memcmp(output + placed.Offset + y * placed.Footprint.RowPitch + x * 4, want, 4) != 0;
        check(wrong == 0, "Xbox list texture copy preserves all pixels");
        readback->Unmap(0, nullptr);
    }
    upload->Release();

    const HMODULE xmem = LoadLibraryW(L"xmem.dll");
    using Reserve = void*(WINAPI*)(void*, SIZE_T, DWORD, UINT64, DWORD);
    using AllocatePages = BOOL(WINAPI*)(UINT64, UINT64*, UINT64*);
    using MapPages = void*(WINAPI*)(void*, UINT64, const UINT64*);
    using FreePages = BOOL(WINAPI*)(UINT64, const UINT64*);
    const auto reserve = reinterpret_cast<Reserve>(GetProcAddress(xmem, "XMemVirtualAlloc"));
    const auto allocate_pages = reinterpret_cast<AllocatePages>(GetProcAddress(xmem, "XMemAllocatePhysicalPages"));
    const auto map_pages = reinterpret_cast<MapPages>(GetProcAddress(xmem, "XMemMapPhysicalPages"));
    const auto free_pages = reinterpret_cast<FreePages>(GetProcAddress(xmem, "XMemFreePhysicalPages"));
    if (!check(reserve && allocate_pages && map_pages && free_pages, "XMem physical pages are available"))
        return finish();

    constexpr UINT64 page_size = 65536;
    UINT64 pages[2]{};
    UINT64 page_count = 2;
    auto* guest = static_cast<std::uint8_t*>(
        reserve(nullptr, page_count * page_size, MEM_RESERVE | 0x20400000u, 0x21, PAGE_READWRITE));
    if (!check(guest && allocate_pages(0x280, &page_count, pages) && page_count == 2, "allocate two XMem pages"))
        return finish();
    const UINT64 reversed[]{pages[1], pages[0]};
    if (!check(map_pages(guest, page_count, reversed) == guest, "map two XMem pages in reverse order"))
        return finish();

    auto physical_desc = buffer_desc;
    physical_desc.Width = page_count * page_size;
    physical_desc.Flags = static_cast<D3D12_RESOURCE_FLAGS>(0x20004u);
    using PlaceBuffer = HRESULT(WINAPI*)(ID3D12Device*, const void*, const D3D12_RESOURCE_DESC*,
                                         D3D12_RESOURCE_STATES, const void*, const IID*, void**);
    auto place = reinterpret_cast<PlaceBuffer>((*reinterpret_cast<void***>(device))[133]);
    const IID resource_iid = __uuidof(ID3D12Resource);
    ID3D12Resource* physical = nullptr;
    const auto xbox_initial_state = static_cast<D3D12_RESOURCE_STATES>(0xffffffffu);
    const HRESULT placed_result = place(device, guest, &physical_desc, xbox_initial_state, nullptr,
                                        &resource_iid, reinterpret_cast<void**>(&physical));
    if (placed_result == E_NOTIMPL) {
        std::printf("tiny_frame: Vulkan host memory import unavailable; skip XMem buffer check\n");
    } else {
        if (!check(SUCCEEDED(placed_result) && physical, "slot 133 imports the guest's XMem buffer"))
            return finish();
        void* refused = physical;
        if (!check(place(device, guest + 1, &physical_desc, xbox_initial_state, nullptr, &resource_iid,
                         &refused) == E_INVALIDARG && !refused, "slot 133 rejects misaligned XMem addresses"))
            return finish();
        std::memcpy(guest, &pattern, sizeof(pattern));
        std::memset(guest + page_size, 0, sizeof(pattern));
        if (!check(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                                       IID_PPV_ARGS(&list))) && list, "create XMem copy list"))
            return finish();
        list->CopyBufferRegion(physical, page_size, physical, 0, sizeof(pattern));
        if (!check(SUCCEEDED(list->Close()), "close XMem copy list")) return finish();
        if (!check(map_pages(guest, page_count, nullptr) == guest, "unmap the guest view before GPU submission"))
            return finish();
        ID3D12CommandList* physical_lists[]{list};
        queue->ExecuteCommandLists(1, physical_lists);
        done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        const bool copied = SUCCEEDED(queue->Signal(fence, 3)) && done &&
                            SUCCEEDED(fence->SetEventOnCompletion(3, done)) &&
                            WaitForSingleObject(done, 30000) == WAIT_OBJECT_0;
        if (done) CloseHandle(done);
        if (!check(copied && map_pages(guest, page_count, reversed) == guest, "remap the guest view after GPU completion"))
            return finish();
        if (!check(std::memcmp(guest + page_size, &pattern, sizeof(pattern)) == 0,
                   "GPU copies between XMem pages through slot 133"))
            return finish();
        struct XboxHeapDesc {
            D3D12_DESCRIPTOR_HEAP_DESC desc;
            void* address;
            UINT64 bytes;
        } placed_heap{{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0},
                      guest, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)};
        ID3D12DescriptorHeap* guest_heap = nullptr;
        if (!check(SUCCEEDED(device->CreateDescriptorHeap(&placed_heap.desc, IID_PPV_ARGS(&guest_heap))) && guest_heap,
                   "create a shader-visible descriptor heap in XMem"))
            return finish();
        auto** heap_table = *reinterpret_cast<void***>(guest_heap);
        const auto cpu_start = reinterpret_cast<HeapStartFn>(heap_table[9])(guest_heap);
        const auto gpu_start = reinterpret_cast<HeapStartFn>(heap_table[10])(guest_heap);
        if (!check(cpu_start == reinterpret_cast<UINT64>(guest) && gpu_start != 0,
                   "placed heap returns the guest CPU address and a Vulkan GPU address"))
            return finish();
        std::memset(guest, 0xff, static_cast<size_t>(placed_heap.bytes));
        device->CreateConstantBufferView(nullptr, {static_cast<SIZE_T>(cpu_start)});
        if (!check(guest[0] != 0xff, "descriptor writes reach guest XMem"))
            return finish();
        struct XboxAddressSrvDesc {
            D3D12_SHADER_RESOURCE_VIEW_DESC view;
            UINT64 address;
        } address_srv{};
        address_srv.view.Format = DXGI_FORMAT_R32_TYPELESS;
        address_srv.view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        address_srv.view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        address_srv.view.Buffer.NumElements = static_cast<UINT>(page_size / sizeof(UINT32));
        address_srv.view.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        address_srv.address = reinterpret_cast<UINT64>(guest + page_size);
        auto address_desc = physical_desc;
        address_desc.Width = page_size;
        address_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        using CreateAddressSrv = void(WINAPI*)(ID3D12Device*, const D3D12_RESOURCE_DESC*,
                                               const XboxAddressSrvDesc*, D3D12_CPU_DESCRIPTOR_HANDLE);
        std::memset(guest, 0, static_cast<size_t>(placed_heap.bytes));
        reinterpret_cast<CreateAddressSrv>((*reinterpret_cast<void***>(device))[70])(
            device, &address_desc, &address_srv, {static_cast<SIZE_T>(cpu_start)});
        UINT64 encoded_address = 0;
        std::memcpy(&encoded_address, guest, sizeof(encoded_address));
        if (!check(encoded_address != 0, "slot 70 imports an address-only buffer SRV into XMem"))
            return finish();
        guest_heap->Release();
        list->Release();
        physical->Release();
    }
    check(map_pages(guest, page_count, nullptr) == guest && free_pages(page_count, pages),
          "release the guest XMem pages");
    VirtualFree(guest, 0, MEM_RELEASE);

    // What a frame owns is released; the device, the queue and the fence stay alive across the return from main, so
    // the run ends through the CRT's exit with the layer still up.
    readback->Release();
    target->Release();
    rtv_heap->Release();
    allocator->Release();
    return finish();
}
