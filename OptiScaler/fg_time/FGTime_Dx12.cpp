#include "FGTime_Dx12.h"

#include <State.h>

#include <include/d3dx/d3dx12.h>

void FGTimeDx12::Init(ID3D12Device* device)
{
    if (_queryHeap != nullptr)
        return;

    D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
    queryHeapDesc.Count = 2;
    queryHeapDesc.NodeMask = 0;
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;

    auto result = device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&_queryHeap));

    if (result != S_OK)
    {
        LOG_ERROR("FGTimeDx12 CreateQueryHeap error: {:X}", (UINT) result);
        return;
    }

    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(2 * sizeof(UINT64));
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    result = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_readbackBuffer));

    if (result != S_OK)
        LOG_ERROR("FGTimeDx12 CreateCommittedResource error: {:X}", (UINT) result);
}

void FGTimeDx12::FGStart(ID3D12GraphicsCommandList* cmdList)
{
    if (_queryHeap != nullptr)
        cmdList->EndQuery(_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
}

void FGTimeDx12::FGEnd(ID3D12GraphicsCommandList* cmdList)
{
    if (_queryHeap != nullptr)
    {
        cmdList->EndQuery(_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 1);

        cmdList->ResolveQueryData(_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, _readbackBuffer, 0);

        _dx12FGTrig = true;
    }
}

void FGTimeDx12::ReadFGTime(ID3D12CommandQueue* commandQueue)
{
    if (_queryHeap == nullptr || !_dx12FGTrig || _readbackBuffer == nullptr)
        return;

    _dx12FGTrig = false;

    UINT64* timestampData = nullptr;
    D3D12_RANGE readRange = { 0, 2 * sizeof(UINT64) };
    HRESULT hr = _readbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&timestampData));

    if (hr == S_OK && timestampData != nullptr)
    {
        UINT64 gpuFrequency;
        if (commandQueue->GetTimestampFrequency(&gpuFrequency) == S_OK && gpuFrequency > 0)
        {
            UINT64 startTime = timestampData[0];
            UINT64 endTime = timestampData[1];
            
            if (endTime >= startTime)
            {
                double elapsedTimeMs = (endTime - startTime) / static_cast<double>(gpuFrequency) * 1000.0;

                // Filter out possibly wrong measured high values
                if (elapsedTimeMs >= 0.0 && elapsedTimeMs < 100.0)
                {
                    State::Instance().frameTimeMutex.lock();
                    State::Instance().fgTimes.push_back(elapsedTimeMs);
                    State::Instance().fgTimes.pop_front();
                    State::Instance().frameTimeMutex.unlock();
                }
            }
        }

        D3D12_RANGE writeRange = { 0, 0 };
        _readbackBuffer->Unmap(0, &writeRange);
    }
    else
    {
        LOG_WARN("FGTimeDx12 Map failed or timestampData is null!");
    }
}
