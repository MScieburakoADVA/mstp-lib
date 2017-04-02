
#pragma once
#include "EventManager.h"
#include "Simulator.h"
#include "mstp-lib/stp.h"

struct BridgeLogLine
{
	std::string text;
	int portIndex;
	int treeIndex;
};

struct StpEnabledEvent : public Event<StpEnabledEvent, void(Bridge*)> { };
struct StpDisablingEvent : public Event<StpDisablingEvent, void(Bridge*)> { };
struct StpVersionChangedEvent : public Event<StpVersionChangedEvent, void(Bridge*)> { };
struct BridgeLogLineGenerated : public Event<BridgeLogLineGenerated, void(Bridge*, const BridgeLogLine& line)> { };

class Bridge : public Object
{
	IProject* const _project;
	float _x;
	float _y;
	float _width;
	float _height;
	std::vector<ComPtr<Port>> _ports;
	std::array<uint8_t, 6> _macAddress;
	bool _powered = true;
	STP_BRIDGE* _stpBridge = nullptr;
	std::mutex _stpBridgeMutex;
	std::thread::id _guiThreadId;
	STP_VERSION _stpVersion = STP_VERSION_RSTP;
	size_t _treeCount = 1;
	static const STP_CALLBACKS StpCallbacks;
	std::vector<BridgeLogLine> _logLines;
	BridgeLogLine _currentLogLine;
	TimerQueueTimer_unique_ptr _oneSecondTimerHandle;
	TimerQueueTimer_unique_ptr _macOperationalTimerHandle;
	HWND_unique_ptr _helperWindow;

	struct RxPacketInfo
	{
		std::vector<uint8_t> data;
		unsigned int portIndex;
		unsigned int timestamp;
	};
	std::queue<RxPacketInfo> _rxQueue;

	// variables used by TransmitGetBuffer/ReleaseBuffer
	std::vector<uint8_t> _txPacketData;
	Port*                _txReceivingPort;
	unsigned int         _txTimestamp;

public:
	Bridge (IProject* project, unsigned int portCount, const std::array<uint8_t, 6>& macAddress);
protected:
	~Bridge();

public:
	static constexpr int HTCodeInner = 1;

	static constexpr float DefaultHeight = 100;
	static constexpr float OutlineWidth = 2;
	static constexpr float MinWidth = 180;
	static constexpr float RoundRadius = 8;

	float GetLeft() const { return _x; }
	float GetRight() const { return _x + _width; }
	float GetTop() const { return _y; }
	float GetBottom() const { return _y + _height; }
	float GetWidth() const { return _width; }
	float GetHeight() const { return _height; }
	void SetLocation (float x, float y);

	D2D1_RECT_F GetBounds() const { return { _x, _y, _x + _width, _y + _height }; }
	const std::vector<ComPtr<Port>>& GetPorts() const { return _ports; }
	std::array<uint8_t, 6> GetMacAddress() const { return _macAddress; }

	virtual void Render (ID2D1RenderTarget* dc, const DrawingObjects& dos, uint16_t vlanNumber) const override final;
	virtual void RenderSelection (const IZoomable* zoomable, ID2D1RenderTarget* rt, const DrawingObjects& dos) const override final;
	virtual HTResult HitTest (const IZoomable* zoomable, D2D1_POINT_2F dLocation, float tolerance) override final;

	StpEnabledEvent::Subscriber GetStpEnabledEvent() { return StpEnabledEvent::Subscriber(_em); }
	StpDisablingEvent::Subscriber GetStpDisablingEvent() { return StpDisablingEvent::Subscriber(_em); }
	StpVersionChangedEvent::Subscriber GetStpVersionChangedEvent() { return StpVersionChangedEvent::Subscriber(_em); }
	BridgeLogLineGenerated::Subscriber GetBridgeLogLineGeneratedEvent() { return BridgeLogLineGenerated::Subscriber(_em); }

	bool IsPowered() const { return _powered; }
	void EnableStp (uint32_t timestamp);
	void DisableStp (uint32_t timestamp);
	bool IsStpEnabled() const { return _stpBridge != nullptr; }
	size_t GetTreeCount() const { return _treeCount; }
	STP_PORT_ROLE GetStpPortRole (size_t portIndex, size_t treeIndex) const;
	bool GetStpPortLearning (size_t portIndex, size_t treeIndex) const;
	bool GetStpPortForwarding (size_t portIndex, size_t treeIndex) const;
	bool GetStpPortOperEdge (size_t portIndex) const;
	bool GetPortAdminEdge (size_t portIndex) const;
	void SetPortAdminEdge (size_t portIndex, bool adminEdge);
	bool GetPortAutoEdge  (size_t portIndex) const;
	void SetPortAutoEdge  (size_t portIndex, bool autoEdge);
	uint16_t GetStpBridgePriority (size_t treeIndex) const;
	size_t GetStpTreeIndexFromVlanNumber (uint16_t vlanNumber) const;
	const std::vector<BridgeLogLine>& GetLogLines() const { return _logLines; }
	bool IsPortForwardingOnVlan (unsigned int portIndex, uint16_t vlanNumber) const;
	bool IsStpRootBridge() const;
	STP_VERSION GetStpVersion() const { return _stpVersion; }
	void SetStpVersion (STP_VERSION stpVersion, uint32_t timestamp);
	void SetStpTreeCount (size_t treeCount);

private:
	static void CALLBACK OneSecondTimerCallback (void* lpParameter, BOOLEAN TimerOrWaitFired);
	static void CALLBACK MacOperationalTimerCallback (void* lpParameter, BOOLEAN TimerOrWaitFired);
	static LRESULT CALLBACK HelperWindowProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
	void ComputeMacOperational();
	void ProcessReceivedPacket();

	static void* StpCallback_AllocAndZeroMemory (unsigned int size);
	static void  StpCallback_FreeMemory (void* p);
	static void* StpCallback_TransmitGetBuffer (STP_BRIDGE* bridge, unsigned int portIndex, unsigned int bpduSize, unsigned int timestamp);
	static void  StpCallback_TransmitReleaseBuffer (STP_BRIDGE* bridge, void* bufferReturnedByGetBuffer);
	static void  StpCallback_EnableLearning (STP_BRIDGE* bridge, unsigned int portIndex, unsigned int treeIndex, bool enable);
	static void  StpCallback_EnableForwarding (STP_BRIDGE* bridge, unsigned int portIndex, unsigned int treeIndex, bool enable);
	static void  StpCallback_FlushFdb (STP_BRIDGE* bridge, unsigned int portIndex, unsigned int treeIndex, enum STP_FLUSH_FDB_TYPE flushType);
	static void  StpCallback_DebugStrOut (STP_BRIDGE* bridge, int portIndex, int treeIndex, const char* nullTerminatedString, unsigned int stringLength, bool flush);
	static void  StpCallback_OnTopologyChange (STP_BRIDGE* bridge);
	static void  StpCallback_OnNotifiedTopologyChange (STP_BRIDGE* bridge, unsigned int portIndex, unsigned int treeIndex);
	static void  StpCallback_OnPortRoleChanged (STP_BRIDGE* bridge, unsigned int portIndex, unsigned int treeIndex, STP_PORT_ROLE role);
};

