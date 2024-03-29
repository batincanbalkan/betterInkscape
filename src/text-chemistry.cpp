// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Text commands
 *
 * Authors:
 *   bulia byak
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <cstring>
#include <string>
#include <glibmm/i18n.h>


#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "inkscape.h"
#include "message-stack.h"
#include "text-chemistry.h"
#include "text-editing.h"
#include "verbs.h"

#include "object/sp-flowdiv.h"
#include "object/sp-flowregion.h"
#include "object/sp-flowtext.h"
#include "object/sp-rect.h"
#include "object/sp-textpath.h"
#include "object/sp-tspan.h"
#include "style.h"

#include "xml/repr.h"

using Inkscape::DocumentUndo;

static SPItem *
flowtext_in_selection(Inkscape::Selection *selection)
{
	auto items = selection->items();
    for(auto i=items.begin();i!=items.end();++i){
        if (SP_IS_FLOWTEXT(*i))
            return *i;
    }
    return nullptr;
}

static SPItem *
text_or_flowtext_in_selection(Inkscape::Selection *selection)
{
    auto items = selection->items();
    for(auto i=items.begin();i!=items.end();++i){
        if (SP_IS_TEXT(*i) || SP_IS_FLOWTEXT(*i))
            return *i;
    }
    return nullptr;
}

static SPItem *
shape_in_selection(Inkscape::Selection *selection)
{
    auto items = selection->items();
    for(auto i=items.begin();i!=items.end();++i){
        if (SP_IS_SHAPE(*i))
            return *i;
    }
    return nullptr;
}

void
text_put_on_path()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!desktop)
        return;

    Inkscape::Selection *selection = desktop->getSelection();

    SPItem *text = text_or_flowtext_in_selection(selection);
    SPItem *shape = shape_in_selection(selection);

    Inkscape::XML::Document *xml_doc = desktop->doc()->getReprDoc();

    if (!text || !shape || boost::distance(selection->items()) != 2) {
        desktop->getMessageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>a text and a path</b> to put text on path."));
        return;
    }

    if (SP_IS_TEXT_TEXTPATH(text)) {
        desktop->getMessageStack()->flash(Inkscape::ERROR_MESSAGE, _("This text object is <b>already put on a path</b>. Remove it from the path first. Use <b>Shift+D</b> to look up its path."));
        return;
    }

    if (SP_IS_RECT(shape)) {
        // rect is the only SPShape which is not <path> yet, and thus SVG forbids us from putting text on it
        desktop->getMessageStack()->flash(Inkscape::ERROR_MESSAGE, _("You cannot put text on a rectangle in this version. Convert rectangle to path first."));
        return;
    }

    // if a flowed text is selected, convert it to a regular text object
    if (SP_IS_FLOWTEXT(text)) {

        if (!SP_FLOWTEXT(text)->layout.outputExists()) {
            desktop->getMessageStack()->
                flash(Inkscape::WARNING_MESSAGE, 
                      _("The flowed text(s) must be <b>visible</b> in order to be put on a path."));
        }

        Inkscape::XML::Node *repr = SP_FLOWTEXT(text)->getAsText();

        if (!repr) return;

        Inkscape::XML::Node *parent = text->getRepr()->parent();
        parent->appendChild(repr);

        SPItem *new_item = (SPItem *) desktop->getDocument()->getObjectByRepr(repr);
        new_item->doWriteTransform(text->transform);
        new_item->updateRepr();

        Inkscape::GC::release(repr);
        text->deleteObject(); // delete the original flowtext

        desktop->getDocument()->ensureUpToDate();

        selection->clear();

        text = new_item; // point to the new text
    }

    Inkscape::Text::Layout const *layout = te_get_layout(text);
    Inkscape::Text::Layout::Alignment text_alignment = layout->paragraphAlignment(layout->begin());

    // remove transform from text, but recursively scale text's fontsize by the expansion
    SP_TEXT(text)->_adjustFontsizeRecursive (text, text->transform.descrim());
    text->setAttribute("transform", nullptr);

    // make a list of text children
    std::vector<Inkscape::XML::Node *> text_reprs;
    for(auto& o: text->children) {
        text_reprs.push_back(o.getRepr());
    }

    // create textPath and put it into the text
    Inkscape::XML::Node *textpath = xml_doc->createElement("svg:textPath");
    // reference the shape
    gchar *href_str = g_strdup_printf("#%s", shape->getRepr()->attribute("id"));
    textpath->setAttribute("xlink:href", href_str);
    g_free(href_str);
    if (text_alignment == Inkscape::Text::Layout::RIGHT) {
        textpath->setAttribute("startOffset", "100%");
    } else if (text_alignment == Inkscape::Text::Layout::CENTER) {
        textpath->setAttribute("startOffset", "50%");
    }
    text->getRepr()->addChild(textpath, nullptr);

    for (auto i=text_reprs.rbegin();i!=text_reprs.rend();++i) {
        // Make a copy of each text child
        Inkscape::XML::Node *copy = (*i)->duplicate(xml_doc);
        // We cannot have multiline in textpath, so remove line attrs from tspans
        if (!strcmp(copy->name(), "svg:tspan")) {
            copy->setAttribute("sodipodi:role", nullptr);
            copy->setAttribute("x", nullptr);
            copy->setAttribute("y", nullptr);
        }
        // remove the old repr from under text
        text->getRepr()->removeChild(*i);
        // put its copy into under textPath
        textpath->addChild(copy, nullptr); // fixme: copy id
    }

    // x/y are useless with textpath, and confuse Batik 1.5
    text->setAttribute("x", nullptr);
    text->setAttribute("y", nullptr);

    DocumentUndo::done(desktop->getDocument(), SP_VERB_CONTEXT_TEXT, 
                       _("Put text on path"));
}

void
text_remove_from_path()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;

    Inkscape::Selection *selection = desktop->getSelection();

    if (selection->isEmpty()) {
        desktop->getMessageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>a text on path</b> to remove it from path."));
        return;
    }

    bool did = false;
    auto items = selection->items();
    for(auto i=items.begin();i!=items.end();++i){
        SPObject *obj = *i;

        if (SP_IS_TEXT_TEXTPATH(obj)) {
            SPObject *tp = obj->firstChild();

            did = true;

            sp_textpath_to_text(tp);
        }
    }

    if (!did) {
        desktop->getMessageStack()->flash(Inkscape::ERROR_MESSAGE, _("<b>No texts-on-paths</b> in the selection."));
    } else {
        DocumentUndo::done(desktop->getDocument(), SP_VERB_CONTEXT_TEXT, 
                           _("Remove text from path"));
        std::vector<SPItem *> vec(selection->items().begin(), selection->items().end());
        selection->setList(vec); // reselect to update statusbar description
    }
}

static void
text_remove_all_kerns_recursively(SPObject *o)
{
    o->setAttribute("dx", nullptr);
    o->setAttribute("dy", nullptr);
    o->setAttribute("rotate", nullptr);

    // if x contains a list, leave only the first value
    gchar const *x = o->getRepr()->attribute("x");
    if (x) {
        gchar **xa_space = g_strsplit(x, " ", 0);
        gchar **xa_comma = g_strsplit(x, ",", 0);
        if (xa_space && *xa_space && *(xa_space + 1)) {
            o->setAttribute("x", *xa_space);
        } else if (xa_comma && *xa_comma && *(xa_comma + 1)) {
            o->setAttribute("x", *xa_comma);
        }
        g_strfreev(xa_space);
        g_strfreev(xa_comma);
    }

    for (auto& i: o->children) {
        text_remove_all_kerns_recursively(&i);
        i.requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_TEXT_LAYOUT_MODIFIED_FLAG);
    }
}

//FIXME: must work with text selection
void
text_remove_all_kerns()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;

    Inkscape::Selection *selection = desktop->getSelection();

    if (selection->isEmpty()) {
        desktop->getMessageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>text(s)</b> to remove kerns from."));
        return;
    }

    bool did = false;

    auto items = selection->items();
    for(auto i=items.begin();i!=items.end();++i){
        SPObject *obj = *i;

        if (!SP_IS_TEXT(obj) && !SP_IS_TSPAN(obj) && !SP_IS_FLOWTEXT(obj)) {
            continue;
        }

        text_remove_all_kerns_recursively(obj);
        obj->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG | SP_TEXT_LAYOUT_MODIFIED_FLAG);
        did = true;
    }

    if (!did) {
        desktop->getMessageStack()->flash(Inkscape::ERROR_MESSAGE, _("Select <b>text(s)</b> to remove kerns from."));
    } else {
        DocumentUndo::done(desktop->getDocument(), SP_VERB_CONTEXT_TEXT, 
                           _("Remove manual kerns"));
    }
}

void
text_flow_into_shape()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!desktop)
        return;

    SPDocument *doc = desktop->getDocument();
    Inkscape::XML::Document *xml_doc = doc->getReprDoc();

    Inkscape::Selection *selection = desktop->getSelection();

    SPItem *text = text_or_flowtext_in_selection(selection);
    SPItem *shape = shape_in_selection(selection);

    if (!text || !shape || boost::distance(selection->items()) < 2) {
        desktop->getMessageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>a text</b> and one or more <b>paths or shapes</b> to flow text into frame."));
        return;
    }

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (prefs->getBool("/tools/text/use_svg2", true)) {
        // SVG 2 Text

        if (SP_IS_TEXT(text)) {

            // Make list of all shapes.
            Glib::ustring shape_inside;
            auto items = selection->items();
            for (auto item : items) {
                if (SP_IS_SHAPE(item)) {
                    shape_inside += "url(#";
                    shape_inside += item->getId();
                    shape_inside += ") ";
                }
            }

            // Remove extra space at end.
            if (shape_inside.length() > 1) {
                shape_inside.erase (shape_inside.length() - 1);
            }

            // Set 'shape-inside' property.
            SPCSSAttr* css = sp_repr_css_attr (text->getRepr(), "style");
            sp_repr_css_set_property (css, "shape-inside", shape_inside.c_str());
            sp_repr_css_set (text->getRepr(), css, "style");
        }

        DocumentUndo::done(doc, SP_VERB_CONTEXT_TEXT,
                           _("Flow text into shape"));


    } else {
        // SVG 1.2 Flowed Text

        if (SP_IS_TEXT(text) || SP_IS_FLOWTEXT(text)) {
            // remove transform from text, but recursively scale text's fontsize by the expansion
            auto ex = i2i_affine(text, shape->parent).descrim();
            SP_TEXT(text)->_adjustFontsizeRecursive(text, ex);
            text->setAttribute("transform", nullptr);
        }

        Inkscape::XML::Node *root_repr = xml_doc->createElement("svg:flowRoot");
        root_repr->setAttribute("xml:space", "preserve"); // we preserve spaces in the text objects we create
        root_repr->setAttribute("style", text->getRepr()->attribute("style")); // fixme: transfer style attrs too
        shape->parent->getRepr()->appendChild(root_repr);
        SPObject *root_object = doc->getObjectByRepr(root_repr);
        g_return_if_fail(SP_IS_FLOWTEXT(root_object));

        Inkscape::XML::Node *region_repr = xml_doc->createElement("svg:flowRegion");
        root_repr->appendChild(region_repr);
        SPObject *object = doc->getObjectByRepr(region_repr);
        g_return_if_fail(SP_IS_FLOWREGION(object));

        /* Add clones */
        auto items = selection->items();
        for(auto i=items.begin();i!=items.end();++i){
            SPItem *item = *i;
            if (SP_IS_SHAPE(item)){
                Inkscape::XML::Node *clone = xml_doc->createElement("svg:use");
                clone->setAttribute("x", "0");
                clone->setAttribute("y", "0");
                gchar *href_str = g_strdup_printf("#%s", item->getRepr()->attribute("id"));
                clone->setAttribute("xlink:href", href_str);
                g_free(href_str);

                // add the new clone to the region
                region_repr->appendChild(clone);
            }
        }

        if (SP_IS_TEXT(text)) { // flow from text, as string
            Inkscape::XML::Node *para_repr = xml_doc->createElement("svg:flowPara");
            root_repr->appendChild(para_repr);
            object = doc->getObjectByRepr(para_repr);
            g_return_if_fail(SP_IS_FLOWPARA(object));

            Inkscape::Text::Layout const *layout = te_get_layout(text);
            Glib::ustring text_ustring = sp_te_get_string_multiline(text, layout->begin(), layout->end());

            Inkscape::XML::Node *text_repr = xml_doc->createTextNode(text_ustring.c_str()); // FIXME: transfer all formatting! and convert newlines into flowParas!
            para_repr->appendChild(text_repr);

            Inkscape::GC::release(para_repr);
            Inkscape::GC::release(text_repr);

        } else { // reflow an already flowed text, preserving paras
            for(auto& o: text->children) {
                if (SP_IS_FLOWPARA(&o)) {
                    Inkscape::XML::Node *para_repr = o.getRepr()->duplicate(xml_doc);
                    root_repr->appendChild(para_repr);
                    object = doc->getObjectByRepr(para_repr);
                    g_return_if_fail(SP_IS_FLOWPARA(object));
                    Inkscape::GC::release(para_repr);
                }
            }
    }

    text->deleteObject(true);

    DocumentUndo::done(doc, SP_VERB_CONTEXT_TEXT,
                       _("Flow text into shape"));

    desktop->getSelection()->set(SP_ITEM(root_object));

    Inkscape::GC::release(root_repr);
    Inkscape::GC::release(region_repr);

    }
}

void
text_unflow ()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;
    if (!desktop)
        return;

    SPDocument *doc = desktop->getDocument();
    Inkscape::XML::Document *xml_doc = doc->getReprDoc();

    Inkscape::Selection *selection = desktop->getSelection();


    if (!flowtext_in_selection(selection) || boost::distance(selection->items()) < 1) {
        desktop->getMessageStack()->flash(Inkscape::WARNING_MESSAGE, _("Select <b>a flowed text</b> to unflow it."));
        return;
    }

    std::vector<SPItem*> new_objs;
    std::vector<SPItem *> old_objs;

    auto items = selection->items();
    for(auto i=items.begin();i!=items.end();++i){

        if (!SP_IS_FLOWTEXT(*i)) {
            continue;
        }

        SPItem *flowtext = *i;

        // we discard transform when unflowing, but we must preserve expansion which is visible as
        // font size multiplier
        double ex = (flowtext->transform).descrim();

        if (sp_te_get_string_multiline(flowtext) == nullptr) { // flowtext is empty
            continue;
        }

        /* Create <text> */
        Inkscape::XML::Node *rtext = xml_doc->createElement("svg:text");
        rtext->setAttribute("xml:space", "preserve"); // we preserve spaces in the text objects we create

        /* Set style */
        rtext->setAttribute("style", flowtext->getRepr()->attribute("style")); // fixme: transfer style attrs too; and from descendants

        Geom::OptRect bbox = flowtext->geometricBounds(flowtext->i2doc_affine());
        if (bbox) {
            Geom::Point xy = bbox->min();
            sp_repr_set_svg_double(rtext, "x", xy[Geom::X]);
            sp_repr_set_svg_double(rtext, "y", xy[Geom::Y]);
        }

        /* Create <tspan> */
        Inkscape::XML::Node *rtspan = xml_doc->createElement("svg:tspan");
        rtspan->setAttribute("sodipodi:role", "line"); // otherwise, why bother creating the tspan?
        rtext->addChild(rtspan, nullptr);

        gchar *text_string = sp_te_get_string_multiline(flowtext);
        Inkscape::XML::Node *text_repr = xml_doc->createTextNode(text_string); // FIXME: transfer all formatting!!!
        free(text_string);
        rtspan->appendChild(text_repr);

        flowtext->parent->getRepr()->appendChild(rtext);
        SPObject *text_object = doc->getObjectByRepr(rtext);

        // restore the font size multiplier from the flowtext's transform
        SPText *text = SP_TEXT(text_object);
        text->_adjustFontsizeRecursive(text, ex);

        new_objs.push_back((SPItem*)text_object);
        old_objs.push_back(flowtext);

        Inkscape::GC::release(rtext);
        Inkscape::GC::release(rtspan);
        Inkscape::GC::release(text_repr);
    }

    selection->clear();
    reverse(new_objs.begin(),new_objs.end());
    selection->setList(new_objs);
    for (auto i:old_objs) {
        i->deleteObject (true);
    }

    DocumentUndo::done(doc, SP_VERB_CONTEXT_TEXT, 
                       _("Unflow flowed text"));
}

void
flowtext_to_text()
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;

    Inkscape::Selection *selection = desktop->getSelection();

    if (selection->isEmpty()) {
        desktop->getMessageStack()->flash(Inkscape::WARNING_MESSAGE, 
                                                 _("Select <b>flowed text(s)</b> to convert."));
        return;
    }

    bool did = false;
    bool ignored = false;

    std::vector<Inkscape::XML::Node*> reprs;
    std::vector<SPItem*> items(selection->items().begin(), selection->items().end());
    for(std::vector<SPItem*>::const_iterator i=items.begin();i!=items.end();++i){
        
        SPItem *item = *i;

        if (!SP_IS_FLOWTEXT(item))
            continue;

        if (!SP_FLOWTEXT(item)->layout.outputExists()) {
            ignored = true;
            continue;
        }

        Inkscape::XML::Node *repr = SP_FLOWTEXT(item)->getAsText();

        if (!repr) break;

        did = true;

        Inkscape::XML::Node *parent = item->getRepr()->parent();
        parent->addChild(repr, item->getRepr());

        SPItem *new_item = reinterpret_cast<SPItem *>(desktop->getDocument()->getObjectByRepr(repr));
        new_item->doWriteTransform(item->transform);
        new_item->updateRepr();
    
        Inkscape::GC::release(repr);
        item->deleteObject();

        reprs.push_back(repr);
    }

    if (did) {
        DocumentUndo::done(desktop->getDocument(), 
                           SP_VERB_OBJECT_FLOWTEXT_TO_TEXT,
                           _("Convert flowed text to text"));
        selection->setReprList(reprs);        
    } else if (ignored) {
        // no message for (did && ignored) because it is immediately overwritten
        desktop->getMessageStack()->
            flash(Inkscape::ERROR_MESSAGE,
                  _("Flowed text(s) must be <b>visible</b> in order to be converted."));

    } else {
        desktop->getMessageStack()->
            flash(Inkscape::ERROR_MESSAGE,
                  _("<b>No flowed text(s)</b> to convert in the selection."));
    }

}


/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
