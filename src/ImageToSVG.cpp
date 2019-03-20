/*************************************************************************
** ImageToSVG.cpp                                                       **
**                                                                      **
** This file is part of dvisvgm -- a fast DVI to SVG converter          **
** Copyright (C) 2005-2019 Martin Gieseking <martin.gieseking@uos.de>   **
**                                                                      **
** This program is free software; you can redistribute it and/or        **
** modify it under the terms of the GNU General Public License as       **
** published by the Free Software Foundation; either version 3 of       **
** the License, or (at your option) any later version.                  **
**                                                                      **
** This program is distributed in the hope that it will be useful, but  **
** WITHOUT ANY WARRANTY; without even the implied warranty of           **
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the         **
** GNU General Public License for more details.                         **
**                                                                      **
** You should have received a copy of the GNU General Public License    **
** along with this program; if not, see <http://www.gnu.org/licenses/>. **
*************************************************************************/

#include <config.h>
#include <fstream>
#include <sstream>
#include "Calculator.hpp"
#include "ImageToSVG.hpp"
#include "Message.hpp"
#include "MessageException.hpp"
#include "PageRanges.hpp"
#include "PsSpecialHandler.hpp"
#include "SVGOptimizer.hpp"
#include "SVGOutput.hpp"
#include "System.hpp"
#include "utility.hpp"
#include "version.hpp"

using namespace std;


void ImageToSVG::checkGSAndFileFormat () {
	if (!_haveGS) {
#ifdef HAVE_LIBGS
		_haveGS = true;
#else
		_haveGS = Ghostscript().available();
#endif
		if (!_haveGS)
			throw MessageException("Ghostscript is required to process "+imageFormat()+" files");
		if (!imageIsValid())
			throw MessageException("invalid "+imageFormat()+" file");
	}
}


void ImageToSVG::convert (int pageno) {
	checkGSAndFileFormat();
	BoundingBox bbox = imageBBox();
	if (bbox.valid() && (bbox.width() == 0 || bbox.height() == 0))
		Message::wstream(true) << "bounding box of " << imageFormat() << " file is empty\n";
	Message::mstream().indent(0);
	Message::mstream(false, Message::MC_PAGE_NUMBER) << "processing " << imageFormat() << " file\n";
	Message::mstream().indent(1);
	_svg.newPage(1);
	// create a psfile special and forward it to the PsSpecialHandler
	stringstream ss;
	ss << "\"" << _fname << "\" "
			"llx=" << bbox.minX() << " "
			"lly=" << bbox.minY() << " "
			"urx=" << bbox.maxX() << " "
			"ury=" << bbox.maxY();
	if (!isSinglePageFormat())
		ss << " page=" << pageno;
	try {
		_psHandler.process(psSpecialCmd(), ss, *this);
	}
	catch (...) {
		progress(0);  // remove progress message
		throw;
	}
	progress(0);
	Matrix matrix = getUserMatrix(_bbox);
	// output SVG file
	SVGOptimizer(_svg).execute();
	_svg.transformPage(matrix);
	_bbox.transform(matrix);
	_svg.setBBox(_bbox);
	_svg.appendToDoc(util::make_unique<XMLComment>(" This file was generated by dvisvgm " + string(PROGRAM_VERSION) + " "));
	bool success = _svg.write(_out.getPageStream(pageno, totalPageCount()));
	string svgfname = _out.filename(pageno, totalPageCount());
	if (svgfname.empty())
		svgfname = "<stdout>";
	if (!success)
		Message::wstream() << "failed to write output to " << svgfname << '\n';
	else {
		const double bp2pt = 72.27/72;
		const double bp2mm = 25.4/72;
		Message::mstream(false, Message::MC_PAGE_SIZE) << "graphic size: " << XMLString(_bbox.width()*bp2pt) << "pt"
			" x " << XMLString(_bbox.height()*bp2pt) << "pt"
			" (" << XMLString(_bbox.width()*bp2mm) << "mm"
			" x " << XMLString(_bbox.height()*bp2mm) << "mm)\n";
		Message::mstream(false, Message::MC_PAGE_WRITTEN) << "output written to " << svgfname << '\n';
	}
	_svg.reset();
}


void ImageToSVG::convert (int firstPage, int lastPage, pair<int,int> *pageinfo) {
	checkGSAndFileFormat();
	int pageCount = 1;       // number of pages converted
	if (isSinglePageFormat())
		convert(1);
	else {
		if (firstPage > lastPage)
			swap(firstPage, lastPage);
		firstPage = max(1, firstPage);
		if (firstPage > totalPageCount())
			pageCount = 0;
		else {
			lastPage = min(totalPageCount(), lastPage);
			pageCount = lastPage-firstPage+1;
			for (int i=firstPage; i <= lastPage; i++)
				convert(i);
		}
	}
	if (pageinfo) {
		pageinfo->first = pageCount;
		pageinfo->second = totalPageCount();
	}
}


void ImageToSVG::convert (const std::string &rangestr, pair<int,int> *pageinfo) {
	checkGSAndFileFormat();
	PageRanges ranges;
	if (!ranges.parse(rangestr, totalPageCount()))
		throw MessageException("invalid page range format");

	int pageCount=0;  // number of pages converted
	for (const auto &range : ranges) {
		convert(range.first, range.second, pageinfo);
		if (pageinfo)
			pageCount += pageinfo->first;
	}
	if (pageinfo)
		pageinfo->first = pageCount;
}


string ImageToSVG::getSVGFilename (unsigned pageno) const {
	if (pageno == 1)
		return _out.filename(1, 1);
	return "";
}


void ImageToSVG::progress (const char *id) {
	static double time=System::time();
	static bool draw=false; // show progress indicator?
	static size_t count=0;
	count++;
	// don't show the progress indicator before the given time has elapsed
	if (!draw && System::time()-time > PROGRESSBAR_DELAY) {
		draw = true;
		Terminal::cursor(false);
		Message::mstream(false) << "\n";
	}
	if (draw && ((System::time() - time > 0.05) || id == 0)) {
		const size_t DIGITS=6;
		Message::mstream(false, Message::MC_PROGRESS)
			<< string(DIGITS-min(DIGITS, static_cast<size_t>(log10(count))), ' ')
			<< count << " PostScript instructions processed\r";
		// overprint indicator when finished
		if (id == 0) {
			Message::estream().clearline();
			Terminal::cursor(true);
		}
		time = System::time();
	}
}


/** Returns the matrix describing the graphics transformations
 *  given by the user in terms of transformation commands.
 *  @param[in] bbox bounding box of the graphics to transform */
Matrix ImageToSVG::getUserMatrix (const BoundingBox &bbox) const {
	Matrix matrix(1);
	if (!_transCmds.empty()) {
		const double bp2pt = (1_bp).pt();
		Calculator calc;
		calc.setVariable("ux", bbox.minX()*bp2pt);
		calc.setVariable("uy", bbox.minY()*bp2pt);
		calc.setVariable("w",  bbox.width()*bp2pt);
		calc.setVariable("h",  bbox.height()*bp2pt);
		// add constants for length units to calculator
		for (auto unit : Length::getUnits())
			calc.setVariable(unit.first, Length(1, unit.second).pt());
		matrix.set(_transCmds, calc);
	}
	return matrix;
}