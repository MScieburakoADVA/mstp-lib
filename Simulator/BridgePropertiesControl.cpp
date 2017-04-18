#include "pch.h"
#include "BridgePropertiesControl.h"
#include "resource.h"
#include "Bridge.h"

static const UINT WM_WORK = WM_APP + 1;

BridgePropertiesControl::BridgePropertiesControl (HWND hwndParent, const RECT& rect, ISelection* selection)
	: _selection(selection)
{
	HINSTANCE hInstance;
	BOOL bRes = GetModuleHandleEx (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR) &DialogProcStatic, &hInstance);
	if (!bRes)
		throw win32_exception(GetLastError());

	_hwnd = CreateDialogParam (hInstance, MAKEINTRESOURCE(IDD_PROPPAGE_BRIDGE), hwndParent, &DialogProcStatic, reinterpret_cast<LPARAM>(this));

	::MoveWindow (_hwnd, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);

	_selection->GetSelectionChangedEvent().AddHandler (&OnSelectionChanged, this);
}


BridgePropertiesControl::~BridgePropertiesControl()
{
	_selection->GetSelectionChangedEvent().RemoveHandler (&OnSelectionChanged, this);

	if (_hwnd != nullptr)
		::DestroyWindow (_hwnd);
}

//static
INT_PTR CALLBACK BridgePropertiesControl::DialogProcStatic (HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	BridgePropertiesControl* window;
	if (uMsg == WM_INITDIALOG)
	{
		window = reinterpret_cast<BridgePropertiesControl*>(lParam);
		window->_hwnd = hwnd;
		assert (GetWindowLongPtr(hwnd, GWLP_USERDATA) == 0);
		SetWindowLongPtr (hwnd, GWLP_USERDATA, reinterpret_cast<LPARAM>(window));
	}
	else
		window = reinterpret_cast<BridgePropertiesControl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

	if (window == nullptr)
	{
		// this must be one of those messages sent before WM_NCCREATE or after WM_NCDESTROY.
		return DefWindowProc (hwnd, uMsg, wParam, lParam);
	}

	Result result = window->DialogProc (uMsg, wParam, lParam);

	if (uMsg == WM_NCDESTROY)
	{
		window->_hwnd = nullptr;
		SetWindowLongPtr (hwnd, GWLP_USERDATA, 0);
	}

	::SetWindowLong (hwnd, DWL_MSGRESULT, result.messageResult);
	return result.dialogProcResult;
}

static const UINT_PTR EditSubClassId = 1;

BridgePropertiesControl::Result BridgePropertiesControl::DialogProc (UINT msg, WPARAM wParam , LPARAM lParam)
{
	if (msg == WM_INITDIALOG)
	{
		_bridgeAddressEdit = GetDlgItem (_hwnd, IDC_EDIT_BRIDGE_ADDRESS);
		BOOL bRes = SetWindowSubclass (_bridgeAddressEdit, EditSubclassProc, EditSubClassId, (DWORD_PTR) this); assert (bRes);
		return { FALSE, 0 };
	}
	
	if (msg == WM_DESTROY)
	{
		BOOL bRes = RemoveWindowSubclass (_bridgeAddressEdit, EditSubclassProc, EditSubClassId); assert (bRes);
		return { FALSE, 0 };
	}

	if (msg == WM_CTLCOLORDLG)
		return { reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW)), 0 };

	if (msg == WM_CTLCOLORSTATIC)
		return { reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW)), 0 };

	if (msg == WM_WORK)
	{
		_workQueue.front()();
		_workQueue.pop();
		return { TRUE, 0 };
	}

	return { FALSE, 0 };
}

//static
LRESULT CALLBACK BridgePropertiesControl::EditSubclassProc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	auto dialog = reinterpret_cast<BridgePropertiesControl*>(dwRefData);
	
	if (msg == WM_CHAR)
	{
		if ((wParam == VK_RETURN) || (wParam == VK_ESCAPE))
			return 0;

		return DefSubclassProc (hWnd, msg, wParam, lParam);
	}

	if (msg == WM_KEYDOWN)
	{
		if (wParam == VK_ESCAPE)
		{
			auto text = dialog->GetEditPropertyText(hWnd);
			::SetWindowText (hWnd, text.c_str());
			::SendMessage (hWnd, EM_SETSEL, 0, -1);
			return 0;
		}
		else if (wParam == VK_RETURN)
		{
			std::wstring str;
			str.resize(GetWindowTextLength (hWnd) + 1);
			GetWindowText (hWnd, str.data(), str.size());

			if ((dialog->GetEditPropertyText(hWnd) != str) && (dialog->_controlBeingValidated == nullptr))
			{
				dialog->_controlBeingValidated = hWnd;
				std::wstring errorMessage;
				bool valid = dialog->ValidateAndSetProperty(hWnd, str, errorMessage);
				if (!valid)
				{
					::MessageBox (dialog->_hwnd, errorMessage.c_str(), 0, 0);
					::SetFocus (hWnd);
				}
				::SendMessage (hWnd, EM_SETSEL, 0, -1);
				dialog->_controlBeingValidated = nullptr;
			}

			return 0;
		}

		return DefSubclassProc (hWnd, msg, wParam, lParam);
	}

	if (msg == WM_KILLFOCUS)
	{
		std::wstring str;
		str.resize(GetWindowTextLength (hWnd) + 1);
		GetWindowText (hWnd, str.data(), str.size());

		if ((dialog->GetEditPropertyText(hWnd) != str) && (dialog->_controlBeingValidated == nullptr))
		{
			dialog->_controlBeingValidated = hWnd;

			std::wstring errorMessage;
			bool valid = dialog->ValidateAndSetProperty(hWnd, str, errorMessage);
			if (valid)
			{
				dialog->_controlBeingValidated = nullptr;
			}
			else
			{
				::SetFocus(nullptr);
				dialog->PostWork ([dialog, hWnd, message=move(errorMessage)]
				{
					::MessageBox (dialog->_hwnd, message.c_str(), 0, 0);
					::SetFocus (hWnd);
					::SendMessage (hWnd, EM_SETSEL, 0, -1);
					dialog->_controlBeingValidated = nullptr;
				});
			}
		}

		return DefSubclassProc (hWnd, msg, wParam, lParam);
	}

	return DefSubclassProc (hWnd, msg, wParam, lParam);
}

//static
void BridgePropertiesControl::OnSelectionChanged (void* callbackArg, ISelection* selection)
{
	auto window = static_cast<BridgePropertiesControl*>(callbackArg);

	bool bridgesSelected = !selection->GetObjects().empty()
		&& all_of (selection->GetObjects().begin(), selection->GetObjects().end(), [](const ComPtr<Object>& o) { return dynamic_cast<Bridge*>(o.Get()) != nullptr; });

	if (bridgesSelected)
	{
		if (selection->GetObjects().size() == 1)
		{
			auto bridge = dynamic_cast<Bridge*>(selection->GetObjects()[0].Get());
			::SetWindowText (window->_bridgeAddressEdit, bridge->GetMacAddressAsString().c_str());
			::EnableWindow (window->_bridgeAddressEdit, TRUE);
		}
		else
		{
			::SetWindowText (window->_bridgeAddressEdit, L"(multiple selection)");
			::EnableWindow (window->_bridgeAddressEdit, FALSE);
		}

		::ShowWindow (window->GetHWnd(), SW_SHOW);
	}
	else
		::ShowWindow (window->GetHWnd(), SW_HIDE);
}

void BridgePropertiesControl::PostWork (std::function<void()>&& work)
{
	_workQueue.push(move(work));
	PostMessage (_hwnd, WM_WORK, 0, 0);
}

std::wstring BridgePropertiesControl::GetEditPropertyText(HWND hwnd) const
{
	if (hwnd == _bridgeAddressEdit)
	{
		auto bridge = dynamic_cast<Bridge*>(_selection->GetObjects()[0].Get());
		return bridge->GetMacAddressAsString();
	}
	else
		throw not_implemented_exception();
}

bool BridgePropertiesControl::ValidateAndSetProperty (HWND hwnd, const std::wstring& str, std::wstring& errorMessageOut)
{
	if (hwnd == _bridgeAddressEdit)
	{
		if (!iswxdigit(str[0]) || !iswxdigit(str[1]))
		{
			errorMessageOut = L"Invalid address format. The Bridge Address must have the format XX:XX:XX:XX:XX:XX.";
			return false;
		}

		return true;
	}
	else
		throw not_implemented_exception();
}
