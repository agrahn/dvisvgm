/***********************************************************************
** DVIToSVG.cpp                                                       **
**                                                                    **
** This file is part of dvisvgm -- the DVI to SVG converter           **
** Copyright (C) 2005-2007 Martin Gieseking <martin.gieseking@uos.de> **
**                                                                    **
** This program is free software; you can redistribute it and/or      **
** modify it under the terms of the GNU General Public License        **
** as published by the Free Software Foundation; either version 2     **
** of the License, or (at your option) any later version.             **
**                                                                    **
** This program is distributed in the hope that it will be useful,    **
** but WITHOUT ANY WARRANTY; without even the implied warranty of     **
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the      **
** GNU General Public License for more details.                       **
**                                                                    **
** You should have received a copy of the GNU General Public License  **
** along with this program; if not, write to the Free Software        **
** Foundation, Inc., 51 Franklin Street, Fifth Floor,                 **
** Boston, MA 02110-1301, USA.                                        **
***********************************************************************/
// $Id$

#include <cstdlib>
#include <ctime>
#include <fstream>
#include "Calculator.h"
#include "CharmapTranslator.h"
#include "DVIBBoxActions.h"
#include "DVIToSVG.h"
#include "DVIToSVGActions.h"
#include "Font.h"
#include "FontManager.h"
#include "KPSFileFinder.h"
#include "Message.h"
#include "PageSize.h"
#include "SVGFontEmitter.h"
#include "SVGFontTraceEmitter.h"
#include "TransformationMatrix.h"
#include "TFM.h"
#include "XMLDocument.h"
#include "XMLDocTypeNode.h"
#include "XMLString.h"


#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#define VERSION ""
#endif

using namespace std;

static string datetime () {
	time_t t;
	time(&t);
	struct tm *tm = localtime(&t);
	char *timestr = asctime(tm);
	timestr[24] = 0;  // remove newline
	return timestr;
}

DVIToSVG::DVIToSVG (istream &is, ostream &os) 
	: DVIReader(is), out(os)
{
	svgDocument = 0;
	svgElement = new XMLElementNode("svg");
	replaceActions(new DVIToSVGActions(*this, svgElement));
	processSpecials = false;
	doctypeNode = 0;
	mag = 4;
}


DVIToSVG::~DVIToSVG () {
	delete svgDocument;
	delete replaceActions(0);
}

/** Starts the conversion process. 
 *  @return number of processed pages */
int DVIToSVG::convert (unsigned firstPage, unsigned lastPage) {
	executePostamble();    // collect scaling and font information
	if (firstPage > getTotalPages()) {
		ostringstream oss;
		oss << "file contains only " << getTotalPages() << " page(s)";
		throw DVIException(oss.str());
	}	
	if (firstPage < 0)
		firstPage = 1;
	
	computeBoundingBox(firstPage);
	if (transCmds != "") {
		Calculator calc;
		calc.setVariable("ux", boundingBox.minX());
		calc.setVariable("uy", boundingBox.minY());
		calc.setVariable("w", boundingBox.width());
		calc.setVariable("h", boundingBox.height());
		calc.setVariable("pt", 1);
		calc.setVariable("in", 72.27);
		calc.setVariable("cm", 72.27/2.54);
		calc.setVariable("mm", 72.27/25.4);
		TransformationMatrix matrix(transCmds, calc);
		static_cast<DVIToSVGActions*>(getActions())->setTransformation(matrix);
		if (pageSizeName == "min")
			boundingBox.transform(matrix);
	}
	if (boundingBox.width() > 0) {
		svgElement->addAttribute("width", XMLString(boundingBox.width())); 		
		svgElement->addAttribute("height", XMLString(boundingBox.height()));
		svgElement->addAttribute("viewBox", boundingBox.toSVGViewBox());
	}
	svgElement->addAttribute("version", "1.1");
	svgElement->addAttribute("xmlns", "http://www.w3.org/2000/svg");

   svgDocument = new XMLDocument(svgElement);
	svgDocument->append(new XMLCommentNode(" This file was generated by dvisvgm "VERSION" "));
	svgDocument->append(new XMLCommentNode(" " + datetime() + " "));
	svgDocument->append(new XMLDocTypeNode("svg", "PUBLIC", 
		"\"-//W3C//DTD SVG 1.1//EN\"\n"
		"  \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\""));

	XMLElementNode *styleElement = new XMLElementNode("style");
	styleElement->addAttribute("type", "text/css");
	svgElement->append(styleElement);
	ostringstream style;
	FORALL(getFontManager()->getFonts(), vector<Font*>::const_iterator, i) {
		if (!dynamic_cast<VirtualFont*>(*i)) {  // skip virtual fonts
			style << "text.f"        << getFontManager()->fontID(*i) << ' '
					<< "{font-family:" << (*i)->name()
					<< ";font-size:"   << (*i)->scaledSize() << "}\n";
		}
	}
/*	if (separateFonts)
		FORALL(getFontInfoMap(), ConstIterator, i) {
			const char *fontname = i->second->getFontName().c_str();
			style << "@font-face {font-family:" << fontname << ";"
				      "src:url(" << sepFontFile << "#" << fontname << ")}\n";
		}
*/	
	XMLCDataNode *cdataNode = new XMLCDataNode(style.str());
	styleElement->append(cdataNode);
	if (executePage(firstPage)) {  // @@ 
		Message::mstream() << endl;
		embedFonts(svgElement);
		//svgDocument->emit(out, 0);
		svgDocument->write(out);
	}
	delete svgDocument;
	svgDocument = 0;
	
	if (boundingBox.width() > 0) 
		Message::mstream() << "\npage size: " << boundingBox.width() << "pt"
			   " x " << boundingBox.height() << "pt"
		  	 	" (" << boundingBox.width()/72.27*25.4 << "mm"
		   	" x " << boundingBox.height()/72.27*25.4 << "mm)\n";
	return 1; // @@
}


bool DVIToSVG::computeBoundingBox (int page) {
	if (pageSizeName == "dvi" || pageSizeName == "min") {
		DVIActions *svgActions  = replaceActions(new DVIBBoxActions(boundingBox));
		executePage(page);
		delete replaceActions(svgActions);
		if (pageSizeName == "dvi") {
			// center page content
			double dx = (getPageWidth()-boundingBox.width())/2;
			double dy = (getPageHeight()-boundingBox.height())/2;
			boundingBox += BoundingBox(-dx, -dy, dx, dy);
		}
	}
	else if (pageSizeName != "none") {
		PageSize pageSize(pageSizeName);
		if (pageSize.valid()) {
			// convention: DVI position (0,0) equals (1in, 1in) relative 
			// to the upper left vertex of the page (see DVI specification)
			const double border = -72.27;
			boundingBox = BoundingBox(border, border, pageSize.widthInPT()+border, pageSize.heightInPT()+border);
		}
		else
			Message::wstream(true) << "invalid page format '" << pageSizeName << "'\n";
	}
	return boundingBox.width() > 0 && boundingBox.height() > 0;
}


void DVIToSVG::embedFonts (XMLElementNode *svgElement) {
	if (!svgElement)
		return; 
	if (!getActions())  // no dvi actions => no chars written => no fonts to embed
		return;
	
	XMLElementNode *defs = new XMLElementNode("defs");
	svgElement->append(defs);
	typedef const map<const Font*, set<int> > UsedCharsMap;
	const DVIToSVGActions *svgActions = static_cast<DVIToSVGActions*>(getActions());
	UsedCharsMap &usedChars = svgActions->getUsedChars();
		
	FORALL(usedChars, UsedCharsMap::const_iterator, i) {
		const Font *font = i->first;
		if (const PhysicalFont *ph_font = dynamic_cast<const PhysicalFont*>(font)) {
			CharmapTranslator *cmt = svgActions->getCharmapTranslator(font);
			if (ph_font->type() == PhysicalFont::MF) {
				SVGFontTraceEmitter emitter(font, *cmt, defs);
				emitter.setMag(mag);
				if (emitter.emitFont(i->second, font->name()) > 0)
					Message::mstream() << endl;
			}
			else if (const char *path = font->path()) { // path to pfb/ttf file
				SVGFontEmitter emitter(path, getFontManager()->encoding(font), *cmt, defs);
				emitter.emitFont(i->second, font->name());
			}
			else
				Message::wstream(true) << "can't embed font '" << font->name() << "'";
		}
		else
			Message::wstream(true) << "can't embed font '" << font->name() << "'";
	}
}


void DVIToSVG::setProcessSpecials (bool ps) {
	DVIToSVGActions *actions = dynamic_cast<DVIToSVGActions*>(getActions());
	if (actions) 
		actions->setProcessSpecials(ps);
}
		

void DVIToSVG::setMetafontMag (double m) {
	mag = m;
	TFM::setMetafontMag(m);
}
