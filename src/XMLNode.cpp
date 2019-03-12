/*************************************************************************
** XMLNode.cpp                                                          **
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

#include <algorithm>
#include <map>
#include <list>
#include <sstream>
#include "utility.hpp"
#include "XMLNode.hpp"
#include "XMLString.hpp"

using namespace std;

bool XMLElement::WRITE_NEWLINES=true;


XMLElement::XMLElement (const string &n) : _name(n) {
}


XMLElement::XMLElement (const XMLElement &node)
	: _name(node._name), _attributes(node._attributes)
{
	for (const auto &child : node._children)
		_children.emplace_back(unique_ptr<XMLNode>(child->clone()));
}


XMLElement::XMLElement (XMLElement &&node)
	: _name(std::move(node._name)), _attributes(std::move(node._attributes)), _children(std::move(node._children))
{
}


void XMLElement::clear () {
	_attributes.clear();
	_children.clear();
}


void XMLElement::addAttribute (const string &name, const string &value) {
	if (Attribute *attr = getAttribute(name))
		attr->value = value;
	else
		_attributes.emplace_back(Attribute(name, value));
}


void XMLElement::addAttribute (const string &name, double value) {
	addAttribute(name, XMLString(value));
}


/** Appends a child node to this element. The element also takes the ownership of the child.
 *  @param[in] child node to be appended
 *  @return raw pointer to the appended child node */
XMLNode* XMLElement::append (unique_ptr<XMLNode> &&child) {
	if (!child)
		return nullptr;
	XMLText *textNode1 = dynamic_cast<XMLText*>(child.get());
	if (!textNode1 || _children.empty())
		_children.emplace_back(std::move(child));
	else {
		if (XMLText *textNode2 = dynamic_cast<XMLText*>(_children.back().get()))
			textNode2->append(util::static_unique_ptr_cast<XMLText>(std::move(child)));  // merge two consecutive text nodes
		else
			_children.emplace_back(std::move(child));
	}
	return _children.back().get();
}


/** Appends a string to this element. If the last child is a text node, the string is
 *  appended there, otherwise a new text node is created.
 *  @param[in] str string to be appended
 *  @return raw pointer to the text node the string was appended to */
XMLNode* XMLElement::append (const string &str) {
	if (_children.empty() || !dynamic_cast<XMLText*>(_children.back().get()))
		_children.emplace_back(util::make_unique<XMLText>(str));
	else
		static_cast<XMLText*>(_children.back().get())->append(str);
	return _children.back().get();
}


/** Prepends a child node to this element. The element also takes the ownership of the child.
 *  @param[in] child node to be prepended
 *  @return raw pointer to the prepended child node */
XMLNode* XMLElement::prepend (unique_ptr<XMLNode> &&child) {
	if (!child)
		return nullptr;
	XMLText *textNode1 = dynamic_cast<XMLText*>(child.get());
	if (textNode1 && !_children.empty()) {
		if (XMLText *textNode2 = dynamic_cast<XMLText*>(_children.front().get())) {
			textNode2->prepend(util::static_unique_ptr_cast<XMLText>(std::move(child)));  // merge two consecutive text nodes
			return textNode2;
		}
	}
	_children.emplace_front(std::move(child));
	return _children.front().get();
}


/** Inserts a new child node before a given child node already present. The latter
 *  will be the following sibling of the node to be inserted. If there's no such
 *  node present, nothing is inserted.
 *  @param[in] child node to be inserted
 *  @param[in] sibling following sibling of 'child'
 *  @return true on success */
bool XMLElement::insertBefore (unique_ptr<XMLNode> &&child, XMLNode *sibling) {
	auto it = _children.begin();
	while (it != _children.end() && it->get() != sibling)
		++it;
	if (it == _children.end())
		return false;
	_children.emplace(it, std::move(child));
	return true;
}


/** Inserts a new child node after a given child node already present. The latter
 *  will be the preceding sibling of the node to be inserted. If there's no such
 *  node present, nothing is inserted.
 *  @param[in] child node to be inserted
 *  @param[in] sibling preceding sibling of 'child'
 *  @return true on success */
bool XMLElement::insertAfter (unique_ptr<XMLNode> &&child, XMLNode *sibling) {
	auto it = _children.begin();
	while (it != _children.end() && it->get() != sibling)
		++it;
	if (it == _children.end())
		return false;
	_children.emplace(++it, std::move(child));
	return true;
}


/** Removes a given child from the element. */
void XMLElement::remove (const XMLNode *child) {
	auto it = find_if(_children.begin(), _children.end(), [=](const unique_ptr<XMLNode> &ptr) {
		return ptr.get() == child;
	});
	if (it != _children.end())
		_children.erase(it);
}


/** Gets all descendant elements with a given name and given attribute.
 *  @param[in] name name of elements to find
 *  @param[in] attrName name of attribute to find
 *  @param[out] descendants all elements found
 *  @return true if at least one element was found  */
bool XMLElement::getDescendants (const char *name, const char *attrName, vector<XMLElement*> &descendants) const {
	for (auto &child : _children) {
		if (XMLElement *elem = dynamic_cast<XMLElement*>(child.get())) {
			if ((!name || elem->getName() == name) && (!attrName || elem->hasAttribute(attrName)))
				descendants.push_back(elem);
			elem->getDescendants(name, attrName, descendants);
		}
	}
	return !descendants.empty();
}


/** Returns the first descendant element that matches the given properties in depth first order.
 *  @param[in] name element name; if 0, all elements are taken into account
 *  @param[in] attrName if not 0, only elements with an attribute of this name are considered
 *  @param[in] attrValue if not 0, only elements with attribute attrName="attrValue" are considered
 *  @return pointer to the found element or 0 */
XMLElement* XMLElement::getFirstDescendant (const char *name, const char *attrName, const char *attrValue) const {
	for (auto &child : _children) {
		if (XMLElement *elem = dynamic_cast<XMLElement*>(child.get())) {
			if (!name || elem->getName() == name) {
				const char *value;
				if (!attrName || (((value = elem->getAttributeValue(attrName)) != 0) && (!attrValue || string(value) == attrValue)))
					return elem;
			}
			if (XMLElement *descendant = elem->getFirstDescendant(name, attrName, attrValue))
				return descendant;
		}
	}
	return nullptr;
}


ostream& XMLElement::write (ostream &os) const {
	os << '<' << _name;
	for (const auto &attrib : _attributes)
		os << ' ' << attrib.name << "='" << attrib.value << '\'';
	if (_children.empty())
		os << "/>";
	else {
		os << '>';
		// Insert newlines around children except text nodes. According to the
		// SVG specification, pure whitespace nodes are ignored by the SVG renderer.
		if (WRITE_NEWLINES && !dynamic_cast<XMLText*>(_children.front().get()))
			os << '\n';
		for (auto it=_children.begin(); it != _children.end(); ++it) {
			(*it)->write(os);
			if (!dynamic_cast<XMLText*>(it->get())) {
				auto next=it;
				if (WRITE_NEWLINES && (++next == _children.end() || !dynamic_cast<XMLText*>(next->get())))
					os << '\n';
			}
		}
		os << "</" << _name << '>';
	}
	return os;
}


/** Returns true if this element has an attribute of given name. */
bool XMLElement::hasAttribute (const string &name) const {
	return getAttribute(name) != nullptr;
}


/** Returns the value of an attribute.
 *  @param[in] name name of attribute
 *  @return attribute value or 0 if attribute doesn't exist */
const char* XMLElement::getAttributeValue (const std::string& name) const {
	if (const Attribute *attr = getAttribute(name))
		return attr->value.c_str();
	return nullptr;
}


XMLElement::Attribute* XMLElement::getAttribute (const string &name) {
	auto it = find_if(_attributes.begin(), _attributes.end(), [&](const Attribute &attr) {
		return attr.name == name;
	});
	return it != _attributes.end() ? &(*it) : nullptr;
}


const XMLElement::Attribute* XMLElement::getAttribute (const string &name) const {
	auto it = find_if(_attributes.begin(), _attributes.end(), [&](const Attribute &attr) {
		return attr.name == name;
	});
	return it != _attributes.end() ? &(*it) : nullptr;
}


//////////////////////

void XMLText::append (unique_ptr<XMLNode> &&node) {
	if (!node)
		return;
	if (dynamic_cast<XMLText*>(node.get()))
		append(util::static_unique_ptr_cast<XMLText>(std::move(node)));
	else {
		// append text representation of the node
		ostringstream oss;
		node->write(oss);
		append(XMLString(oss.str()));
	}
}


void XMLText::append (unique_ptr<XMLText> &&node) {
	if (node)
		_text += node->_text;
}


void XMLText::append (const string &str) {
	_text += str;
}


void XMLText::prepend (unique_ptr<XMLNode> &&node) {
	if (XMLText *textNode = dynamic_cast<XMLText*>(node.get()))
		_text = textNode->_text + _text;
}

/////////////////////////////////////////////////////////////////////

ostream& XMLCData::write (ostream &os) const {
	if (!_data.empty())
		os << "<![CDATA[\n" << _data << "]]>";
	return os;
}


void XMLCData::append (string &&str) {
	if (_data.empty())
		_data = move(str);
	else
		_data += str;
}
