#include "stdafx.h"
#include "ListBoxST.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	MASK_DWDATA		0x01	// dwItemData is valid
#define	MASK_LPDATA		0x02	// pData is valid
#define	MASK_NIMAGE		0x04	// nImage is valid
#define	MASK_DWFLAGS	0x08	// dwFlags is valid
#define MASK_ALL		0xff	// All fields are valid
#define TEST_BIT0		0x00000001
#define	LBST_CX_BORDER	3
#define	LBST_CY_BORDER	2

CListBoxST::CListBoxST()
{
	// No image list associated
	m_pImageList = NULL;
	memset(&m_szImage, 0, sizeof m_szImage);

	// By default, hilight full list box item
	SetRowSelect(ST_FULLROWSELECT, FALSE);
}

CListBoxST::~CListBoxST()
{
}

BEGIN_MESSAGE_MAP(CListBoxST, CListBox)
	//{{AFX_MSG_MAP(CListBoxST)
	ON_WM_DESTROY()
	ON_CONTROL_REFLECT_EX(LBN_DBLCLK, OnReflectedDblclk)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

void CListBoxST::OnDestroy()
{
	FreeResources();
	CListBox::OnDestroy();
} // End of OnDestroy

void CListBoxST::MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	ASSERT(lpMeasureItemStruct->CtlType == ODT_LISTBOX);
	CDC* pDC = GetDC();

	CString sText;
	CListBox::GetText(lpMeasureItemStruct->itemID, sText);

	CRect csRect(0, 0, lpMeasureItemStruct->itemWidth, lpMeasureItemStruct->itemHeight);
	int nHeight = pDC->DrawText(sText, -1, csRect, DT_WORDBREAK | DT_EXPANDTABS | DT_CALCRECT);

	lpMeasureItemStruct->itemHeight = m_pImageList ? max(nHeight, m_szImage.cy + LBST_CY_BORDER*2) : nHeight;

	lpMeasureItemStruct->itemHeight += LBST_CY_BORDER*2;

	ReleaseDC(pDC);
} // End of MeasureItem

void CListBoxST::DrawItem(LPDRAWITEMSTRUCT lpDIStruct)
{
	STRUCT_LBDATA *lpLBData = (STRUCT_LBDATA *)CListBox::GetItemDataPtr(lpDIStruct->itemID);
	if (lpLBData == NULL || lpLBData == (LPVOID)-1)
		return;

	CDC* pDC = CDC::FromHandle(lpDIStruct->hDC);
	COLORREF crNormal = GetSysColor(COLOR_WINDOW);
	COLORREF crSelected = GetSysColor(COLOR_HIGHLIGHT);
	COLORREF crText = GetSysColor(COLOR_WINDOWTEXT);
	COLORREF crColor = RGB(0, 0, 0);
	CString sText; // List box item text
	BOOL bIsSelected = (lpDIStruct->itemState & ODS_SELECTED);
	BOOL bIsFocused = (lpDIStruct->itemState & ODS_FOCUS);
	BOOL bIsDisabled = ((lpDIStruct->itemState & ODS_DISABLED) || ((lpLBData->dwFlags & TEST_BIT0) == TEST_BIT0));

	CRect rcItem = lpDIStruct->rcItem;
	CRect rcIcon = lpDIStruct->rcItem;
	CRect rcText = lpDIStruct->rcItem;

	pDC->SetBkMode(TRANSPARENT);

	// ONLY FOR DEBUG
	//CBrush brBtnShadow(RGB(255, 0, 0));
	//pDC->FrameRect(&rcItem, &brBtnShadow);

	// Calculate rcIcon
	if (m_pImageList)
	{
		rcIcon.right = rcIcon.left + m_szImage.cx + LBST_CX_BORDER*2;
		rcIcon.bottom = rcIcon.top + m_szImage.cy + LBST_CY_BORDER*2;
	} // if
	else rcIcon.SetRect(0, 0, 0, 0);

	// Calculate rcText
	rcText.left = rcIcon.right;

	// Calculate rcCenteredText
	// Get list box item text
	CListBox::GetText(lpDIStruct->itemID, sText);
	CRect rcCenteredText = rcText;
	pDC->DrawText(sText, -1, rcCenteredText, DT_WORDBREAK | DT_EXPANDTABS| DT_CALCRECT | lpLBData->nFormat);
	rcCenteredText.OffsetRect(0, (rcText.Height() - rcCenteredText.Height())/2);

	// Draw rcIcon background
	if (m_pImageList)
	{
		if (bIsSelected && (m_byRowSelect == ST_FULLROWSELECT) && !bIsDisabled)
			crColor = crSelected;
		else
			crColor = crNormal;

		OnDrawIconBackground(lpDIStruct->itemID, pDC, &rcItem, &rcIcon, bIsDisabled, bIsSelected, crColor);
	} // if

	// Draw rcText/rcCenteredText background
	if (bIsDisabled)
	{
		pDC->SetTextColor(GetSysColor(COLOR_GRAYTEXT));
		crColor = crNormal;
	} // if
	else
	{
		if (bIsSelected)
		{
			pDC->SetTextColor(0x00FFFFFF & ~crText);
			crColor = crSelected;
		} // if
		else
		{
			pDC->SetTextColor(crText);
			crColor = crNormal;
		} // else
	} // else

	if (m_byRowSelect == ST_TEXTSELECT)
		//pDC->FillSolidRect(&rcCenteredText, crColor);
		OnDrawTextBackground(lpDIStruct->itemID, pDC, &rcItem, &rcCenteredText, bIsDisabled, bIsSelected, crColor);
	else
		//pDC->FillSolidRect(&rcText, crColor);
		OnDrawTextBackground(lpDIStruct->itemID, pDC, &rcItem, &rcText, bIsDisabled, bIsSelected, crColor);

	// Draw the icon (if any)
	if (m_pImageList)
		OnDrawIcon(lpDIStruct->itemID, pDC, &rcItem, &rcIcon, lpLBData->nImage, bIsDisabled, bIsSelected);

	// Draw text
	pDC->DrawText(sText, -1, rcCenteredText, DT_WORDBREAK | DT_EXPANDTABS | lpLBData->nFormat);

	// Draw focus rectangle
	if (bIsFocused && !bIsDisabled)
	{
		switch (m_byRowSelect)
		{
			case ST_FULLROWSELECT:
				pDC->DrawFocusRect(&rcItem);
				break;
			case ST_FULLTEXTSELECT:
				pDC->DrawFocusRect(&rcText);
				break;
			case ST_TEXTSELECT:
			default:
				pDC->DrawFocusRect(&rcCenteredText);
		} // switch
	} // if
} // End of DrawItem

// This function is called every time the background of the area that will contain
// the icon of a list box item needs to be drawn.
// This is a virtual function that can be rewritten in CListBoxST-derived classes
// to produce a whole range of effects not available by default.
// If the list box has no image list associated this function will not be called.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[IN]	pDC
//				Pointer to a CDC object that indicates the device context.
//		[IN]	prcItem
//				Pointer to a CRect object that indicates the bounds of the whole
//				list box item (icon + text).
//		[IN]	prcIcon
//				Pointer to a CRect object that indicates the bounds of the
//				area where the icon should be drawn.
//		[IN]	bIsDisabled
//				TRUE if the list box or the item is disabled, otherwise FALSE.
//		[IN]	bIsSelected
//				TRUE if the list box item is selected, otherwise FALSE.
//		[IN]	crSuggestedColor
//				A COLORREF value containing a suggested color for the background.
//
// Return value:
//		0 (Zero)
//			Function executed successfully.
//
DWORD CListBoxST::OnDrawIconBackground(int /*nIndex*/, CDC* pDC, CRect* prcItem, CRect* prcIcon, BOOL /*bIsDisabled*/, BOOL /*bIsSelected*/, COLORREF crSuggestedColor)
{
	pDC->SetBkColor(crSuggestedColor);
	pDC->FillSolidRect(prcIcon->left, prcIcon->top, prcIcon->Width(), prcItem->Height(), crSuggestedColor);

	return 0;
} // End of OnDrawIconBackground

// This function is called every time the background of the area that will contain
// the text of a list box item needs to be drawn.
// This is a virtual function that can be rewritten in CListBoxST-derived classes
// to produce a whole range of effects not available by default.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[IN]	pDC
//				Pointer to a CDC object that indicates the device context.
//		[IN]	prcItem
//				Pointer to a CRect object that indicates the bounds of the whole
//				list box item (icon + text).
//		[IN]	prcText
//				Pointer to a CRect object that indicates the bounds of the
//				area where the text should be drawn. This area reflects the current
//				row selection type.
//		[IN]	bIsDisabled
//				TRUE if the list box or the item is disabled, otherwise FALSE.
//		[IN]	bIsSelected
//				TRUE if the list box item is selected, otherwise FALSE.
//		[IN]	crSuggestedColor
//				A COLORREF value containing a suggested color for the background.
//
// Return value:
//		0 (Zero)
//			Function executed successfully.
//
DWORD CListBoxST::OnDrawTextBackground(int /*nIndex*/, CDC* pDC, CRect* /*prcItem*/, CRect* prcText, BOOL /*bIsDisabled*/, BOOL /*bIsSelected*/, COLORREF crSuggestedColor)
{
	pDC->SetBkColor(crSuggestedColor);
	pDC->FillSolidRect(prcText, crSuggestedColor);

	return 0;
} // End of OnDrawTextBackground

// This function is called every time the icon of a list box item needs to be drawn.
// This is a virtual function that can be rewritten in CListBoxST-derived classes
// to produce a whole range of effects not available by default.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[IN]	pDC
//				Pointer to a CDC object that indicates the device context.
//		[IN]	prcItem
//				Pointer to a CRect object that indicates the bounds of the whole
//				list box item (icon + text).
//		[IN]	prcIcon
//				Pointer to a CRect object that indicates the bounds of the
//				area where the icon should be drawn.
//		[IN]	nImage
//				The zero-based index of the image associated with the list box item.
//		[IN]	bIsDisabled
//				TRUE if the list box or the item is disabled, otherwise FALSE.
//		[IN]	bIsSelected
//				TRUE if the list box item is selected, otherwise FALSE.
//
// Return value:
//		0 (Zero)
//			Function executed successfully.
//
DWORD CListBoxST::OnDrawIcon(int /*nIndex*/, CDC* pDC, CRect* /*prcItem*/, CRect* prcIcon, int nImage, BOOL bIsDisabled, BOOL /*bIsSelected*/)
{
	HICON hIcon = m_pImageList->ExtractIcon(nImage);
	if (hIcon)
	{
		CPoint	Point(prcIcon->left + LBST_CX_BORDER, prcIcon->top + LBST_CY_BORDER);
		CSize	Size(m_szImage);

		// Ole'!
		pDC->DrawState(	Point,
						Size,
						hIcon,
						(bIsDisabled ? DSS_DISABLED : DSS_NORMAL),
						(CBrush *)NULL);

		::DestroyIcon(hIcon);
	} // if

	return 0;
} // End of OnDrawIcon

BOOL CListBoxST::OnReflectedDblclk()
{
	BOOL	bOutside = FALSE;
	DWORD	dwPos = ::GetMessagePos();
	CPoint	Point((int)LOWORD(dwPos), (int)HIWORD(dwPos));

	ScreenToClient(&Point);
	INT nIndex = ItemFromPoint(Point, bOutside);
	if (!bOutside)
		return !IsItemEnabled(nIndex);

	return FALSE;
} // End of OnReflectedDblclk

void CListBoxST::FreeResources()
{
	int	nCount = GetCount();
	if (nCount != LB_ERR)
		for (;nCount > 0; nCount--)
		{
			DeleteItemData(nCount-1);
		} // for
} // End of FreeResources

int CListBoxST::ReplaceItemData(int nIndex, DWORD_PTR dwItemData, LPVOID pData, int nImage, DWORD dwFlags, BYTE byMask)
{
	int nRetValue = LB_ERR;

	// Get pointer to associated datas (if any)
	STRUCT_LBDATA *lpLBData = (STRUCT_LBDATA *)CListBox::GetItemDataPtr(nIndex);
	// If no datas exist create a new one
	if (lpLBData == NULL)
	{
		try {
			lpLBData = new STRUCT_LBDATA();
		} catch (...) {
			return nRetValue;
		}
	} // if

	if (lpLBData)
	{
		if ((byMask & MASK_DWDATA) == MASK_DWDATA)
			lpLBData->dwItemData = dwItemData;
		if ((byMask & MASK_LPDATA) == MASK_LPDATA)
			lpLBData->pData = pData;
		if ((byMask & MASK_NIMAGE) == MASK_NIMAGE)
			lpLBData->nImage = nImage;
		if ((byMask & MASK_DWFLAGS) == MASK_DWFLAGS)
			lpLBData->dwFlags = dwFlags;

		nRetValue = CListBox::SetItemDataPtr(nIndex, lpLBData);
	} // if

	return nRetValue;
} // End of ReplaceItemData

void CListBoxST::DeleteItemData(int nIndex)
{
	// Get pointer to associated datas (if any)
	STRUCT_LBDATA *lpLBData = (STRUCT_LBDATA *)CListBox::GetItemDataPtr(nIndex);
	// If datas exist
	if (lpLBData != (LPVOID)-1L)
		delete lpLBData;

	CListBox::SetItemDataPtr(nIndex, NULL);
} // End of DeleteItemData

// Adds a string to the list box.
//
// Parameters:
//		[IN]	lpszItem
//				Points to the null-terminated string that is to be added.
//		[IN]	nImage
//				Image to be associated with the string.
//				Pass -1L to associate no image.
//
// Return value:
//		The zero-based index of the string in the list box.
//		The return value is LB_ERR if an error occurs; the return value
//		is LB_ERRSPACE if insufficient space is available to store the new string.
//
int CListBoxST::AddString(LPCTSTR lpszItem, int nImage)
{
	int nIndex = CListBox::AddString(lpszItem);
	if (nIndex != LB_ERR && nIndex != LB_ERRSPACE)
		ReplaceItemData(nIndex, 0, NULL, nImage, 0, MASK_ALL);

	return nIndex;
} // End of AddString

// Inserts a string at a specific location in the list box.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the position to insert the string.
//				If this parameter is -1, the string is added to the end of the list.
//		[IN]	lpszItem
//				Pointer to the null-terminated string that is to be inserted.
//		[IN]	nImage
//				Image to be associated with the string.
//				Pass -1L to associate no image.
//
// Return value:
//		The zero-based index of the position at which the string was inserted.
//		The return value is LB_ERR if an error occurs; the return value
//		is LB_ERRSPACE if insufficient space is available to store the new string.
//
int CListBoxST::InsertString(int nIndex, LPCTSTR lpszString, int nImage)
{
	int nNewIndex = CListBox::InsertString(nIndex, lpszString);
	if (nNewIndex != LB_ERR && nNewIndex != LB_ERRSPACE)
		ReplaceItemData(nNewIndex, 0, NULL, nImage, 0, MASK_ALL);

	return nNewIndex;
} // End of InsertString

// Deletes a string from the list box.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the string to be deleted.
//
// Return value:
//		A count of the strings remaining in the list box.
//		The return value is LB_ERR if nIndex specifies an index greater than
//		the number of items in the list.
//
int CListBoxST::DeleteString(int nIndex)
{
	DeleteItemData(nIndex);
	return CListBox::DeleteString(nIndex);
} // End of DeleteString

// Replaces a string at a specific location in the list box.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the position to replace the string.
//		[IN]	lpszItem
//				Pointer to the null-terminated string that is to be replaced.
//		[IN]	nImage
//				Image to be associated with the string.
//				Pass -1L to associate no image.
//
// Return value:
//		The zero-based index of the position at which the string was replaced.
//		The return value is LB_ERR if an error occurs; the return value
//		is LB_ERRSPACE if insufficient space is available to store the new string.
//
int CListBoxST::ReplaceString(int nIndex, LPCTSTR lpszString, int nImage)
{
	int nRetValue = DeleteString(nIndex);
	if (nRetValue != LB_ERR)
		nRetValue = InsertString(nIndex, lpszString, nImage);

	return nRetValue;
} // End of ReplaceString

// Clears all the entries from the list box.
//
void CListBoxST::ResetContent()
{
	FreeResources();
	CListBox::ResetContent();
} // End of ResetContent

// Sets the 32/64-bit value associated with the list box item.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[IN]	dwItemData
//				Specifies the value to be associated with the item.
//
// Return value:
//		LB_ERR if an error occurs.
//
int CListBoxST::SetItemData(int nIndex, DWORD_PTR dwItemData)
{
	return ReplaceItemData(nIndex, dwItemData, NULL, 0, 0, MASK_DWDATA);
} // End of SetItemData

// Returns the 32/64-bit value associated with the list box item.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//
// Return value:
//		The value associated with the item, or LB_ERR if an error occurs.
//
DWORD_PTR CListBoxST::GetItemData(int nIndex)
{
	STRUCT_LBDATA *lpLBData = (STRUCT_LBDATA *)CListBox::GetItemDataPtr(nIndex);
	if (lpLBData != (LPVOID)-1L)
		return lpLBData->dwItemData;

	return (DWORD_PTR)LB_ERR;
} // End of GetItemData

// Sets a pointer to a list box item.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[IN]	pData
//				Specifies the pointer to be associated with the item.
//
// Return value:
//		LB_ERR if an error occurs.
//
int CListBoxST::SetItemDataPtr(int nIndex, void* pData)
{
	return ReplaceItemData(nIndex, 0, pData, 0, 0, MASK_LPDATA);
} // End of SetItemDataPtr

// Returns a pointer of a list box item.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//
// Return value:
//		Pointer associated with the item, or -1 if an error occurs.
//
void* CListBoxST::GetItemDataPtr(int nIndex)
{
	STRUCT_LBDATA *lpLBData = (STRUCT_LBDATA *)CListBox::GetItemDataPtr(nIndex);
	return (lpLBData != (LPVOID)-1) ? lpLBData->pData : (LPVOID)-1;
} // End of GetItemDataPtr

int CListBoxST::Move(int nOldIndex, int nNewIndex, BOOL bSetCurSel)
{
	// If index is out of range
	if ((UINT)nOldIndex >= (UINT)GetCount())
		return LB_ERR;

	CString sText;
	STRUCT_LBDATA csLBData;
	// Get item text
	GetText(nOldIndex, sText);
	// Get associated data
	STRUCT_LBDATA *lpLBData = (STRUCT_LBDATA *)CListBox::GetItemData(nOldIndex);
	if (lpLBData != (LPVOID)-1)
		memcpy(&csLBData, lpLBData, sizeof csLBData);
	else
		memset(&csLBData, 0, sizeof csLBData);
	// Delete string
	DeleteString(nOldIndex);
	// Insert string at new position
	int nInsertedIndex = InsertString(nNewIndex, sText);
	// Restore associated data
	ReplaceItemData(nInsertedIndex, csLBData.dwItemData, csLBData.pData, csLBData.nImage, csLBData.dwFlags, MASK_ALL);

	// Select item
	if (bSetCurSel && nInsertedIndex != LB_ERR && nInsertedIndex != LB_ERRSPACE)
		SetCurSel(nInsertedIndex);

	return nInsertedIndex;
} // End of Move

// Moves a list box item up by one position
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[IN]	bSetCurSel
//				If TRUE the item will be hilighted
//
// Return value:
//		The zero-based index of the position at which the string was moved.
//		The return value is LB_ERR if an error occurs; the return value
//		is LB_ERRSPACE if insufficient space is available to store the string.
//
int CListBoxST::MoveUp(int nIndex, BOOL bSetCurSel)
{
	return (nIndex > 0) ? Move(nIndex, nIndex - 1, bSetCurSel) : nIndex;
} // End of MoveUp

// Moves a list box item down by one position
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[IN]	bSetCurSel
//				If TRUE the item will be hilighted
//
// Return value:
//		The zero-based index of the position at which the string was moved.
//		The return value is LB_ERR if an error occurs; the return value
//		is LB_ERRSPACE if insufficient space is available to store the string.
//
int CListBoxST::MoveDown(int nIndex, BOOL bSetCurSel)
{
	return (nIndex < GetCount() - 1) ? Move(nIndex, nIndex + 1, bSetCurSel) : nIndex;
} // End of MoveDown

// Moves a list box item to the top most position
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[IN]	bSetCurSel
//				If TRUE the item will be hilighted
//
// Return value:
//		The zero-based index of the position at which the string was moved.
//		The return value is LB_ERR if an error occurs; the return value
//		is LB_ERRSPACE if insufficient space is available to store the string.
//
int CListBoxST::MoveTop(int nIndex, BOOL bSetCurSel)
{
	return (nIndex > 0) ? Move(nIndex, 0, bSetCurSel) : nIndex;
} // End of MoveTop

// Moves a list box item to the bottom most position
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[IN]	bSetCurSel
//				If TRUE the item will be hilighted
//
// Return value:
//		The zero-based index of the position at which the string was moved.
//		The return value is LB_ERR if an error occurs; the return value
//		is LB_ERRSPACE if insufficient space is available to store the string.
//
int CListBoxST::MoveBottom(int nIndex, BOOL bSetCurSel)
{
	return (nIndex < GetCount() - 1) ? Move(nIndex, GetCount() - 1, bSetCurSel) : nIndex;
} // End of MoveBottom

// Enables or disables a list box item.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[IN]	bEnable
//				Specifies whether the given item is to be enabled or disabled.
//				If this parameter is TRUE, the item will be enabled.
//				If this parameter is FALSE, the item will be disabled.
//		[IN]	bRepaint
//				If TRUE the control will be repainted.
//
void CListBoxST::EnableItem(int nIndex, BOOL bEnable, BOOL bRepaint)
{
	// Get pointer to associated datas (if any)
	STRUCT_LBDATA *lpLBData = (STRUCT_LBDATA *)CListBox::GetItemDataPtr(nIndex);
	if (lpLBData != NULL && lpLBData != (LPVOID)-1L)
	{
		if (bEnable)
			ReplaceItemData(nIndex, 0, NULL, 0, (lpLBData->dwFlags & ~TEST_BIT0), MASK_DWFLAGS);
		else
			ReplaceItemData(nIndex, 0, NULL, 0, (lpLBData->dwFlags | TEST_BIT0), MASK_DWFLAGS);

		if (bRepaint)
			Invalidate();
	} // if
} // End of EnableItem

// Specifies whether a list box item is enabled.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//
// Return value:
//		TRUE if the item is enabled; otherwise FALSE.
//
BOOL CListBoxST::IsItemEnabled(int nIndex)
{
	// Get pointer to associated datas (if any)
	STRUCT_LBDATA *lpLBData = (STRUCT_LBDATA*)CListBox::GetItemDataPtr(nIndex);
	if (lpLBData != NULL && lpLBData != (LPVOID)-1L)
	{
		return !((lpLBData->dwFlags & TEST_BIT0) == TEST_BIT0);
		//if (lpLBData->bDisabled)	return FALSE;
	} // if

	return TRUE;
} // End of IsItemEnabled

// Sets how a selected list box item will be hilighted.
//
// Parameters:
//		[IN]	byRowSelect
//				Selection type. Can be one of the following values:
//				ST_FULLROWSELECT	Hilight full list box item (Default)
//				ST_FULLTEXTSELECT	Hilight half list box item (Part containing text)
//				ST_TEXTSELECT		Hilight only list box text
//		[IN]	bRepaint
//				If TRUE the control will be repainted.
//
void CListBoxST::SetRowSelect(BYTE byRowSelect, BOOL bRepaint)
{
	switch (byRowSelect)
	{
		case ST_FULLROWSELECT:
		case ST_FULLTEXTSELECT:
		case ST_TEXTSELECT:
			// Store new selection type
			m_byRowSelect = byRowSelect;

			if (bRepaint)
				Invalidate();
			break;
		default:
			// Bad value
			ASSERT(FALSE);
	} // switch
} // End of SetRowSelect

// Sets the image list to use in the list box.
//
// Parameters:
//		[IN]	pImageList
//				Pointer to an CImageList object containing the image list
//				to use in the list box. Pass NULL to remove any previous
//				associated image list.
//
void CListBoxST::SetImageList(CImageList* pImageList)
{
	m_pImageList = pImageList;
	// Get icons size
	if (m_pImageList)
		ImageList_GetIconSize(*m_pImageList, (LPINT)&m_szImage.cx, (LPINT)&m_szImage.cy);
	else
		memset(&m_szImage, 0, sizeof m_szImage);

	Invalidate();
} // End of SetImageList

// Sets the image to use in a list box item
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[IN]	nImage
//				Specifies the zero-based index of the image
//				inside the imagelist to use.
//		[IN]	bRepaint
//				If TRUE the control will be repainted.
//
void CListBoxST::SetImage(int nIndex, int nImage, BOOL bRepaint)
{
	ReplaceItemData(nIndex, 0, NULL, nImage, 0, MASK_NIMAGE);

	if (bRepaint)	Invalidate();
} // End of SetImage

// Returns the image index associated to a list box item.
//
// Parameters:
//		[IN]	nIndex
//				Specifies the zero-based index of the item.
//		[OUT]	lpnImage
//				Pointer to a int variable that will receive the index
//				of the image inside the imagelist.
//				This variable will be set to -1L if no image is associated.
//
void CListBoxST::GetImage(int nIndex, LPINT lpnImage)
{
	ASSERT(lpnImage != NULL);

	if (lpnImage)
	{
		// Get pointer to associated datas (if any)
		STRUCT_LBDATA *lpLBData = (STRUCT_LBDATA *)CListBox::GetItemDataPtr(nIndex);
		if (lpLBData != NULL && lpLBData != (LPVOID)-1L)
			*lpnImage = lpLBData->nImage;
		else
			*lpnImage = -1L;
	} // if
} // End of GetImage

#undef	MASK_DWDATA
#undef	MASK_LPDATA
#undef	MASK_NIMAGE
#undef	MASK_DWFLAGS
#undef	MASK_ALL
#undef	TEST_BIT0
#undef	LBST_CX_BORDER
#undef	LBST_CY_BORDER