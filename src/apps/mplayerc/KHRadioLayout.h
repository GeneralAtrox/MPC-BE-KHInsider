#pragma once
#include <windows.h>
#include <algorithm>

namespace KHRadioLayout {
struct Viewport {
	bool horizontal = false;
	bool vertical = false;
	SIZE client = {};
};

inline Viewport Measure(SIZE available, SIZE content, int scrollbarWidth, int scrollbarHeight)
{
	Viewport view;
	// Start without scrollbars, so enlarging a previously small panel can
	// remove both bars even when their old combined footprint would not fit.
	for (int pass = 0; pass < 3; ++pass) {
		view.client = {std::max<LONG>(0, available.cx-(view.vertical ? scrollbarWidth : 0)),
			std::max<LONG>(0, available.cy-(view.horizontal ? scrollbarHeight : 0))};
		view.horizontal = content.cx > view.client.cx;
		view.vertical = content.cy > view.client.cy;
	}
	return view;
}

inline RECT Place(RECT original, UINT dpi, UINT referenceDpi, SIZE natural, SIZE client, bool stretchHeight)
{
	RECT r = {MulDiv(original.left, dpi, referenceDpi), MulDiv(original.top, dpi, referenceDpi),
		MulDiv(original.right, dpi, referenceDpi), MulDiv(original.bottom, dpi, referenceDpi)};
	r.right += std::max<LONG>(0, client.cx-natural.cx);
	if (stretchHeight) { r.bottom += std::max<LONG>(0, client.cy-natural.cy); }
	return r;
}
}
