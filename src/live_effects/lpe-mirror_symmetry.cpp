// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * LPE <mirror_symmetry> implementation: mirrors a path with respect to a given line.
 */
/*
 * Authors:
 *   Maximilian Albert
 *   Johan Engelen
 *   Abhishek Sharma
 *   Jabiertxof
 *
 * Copyright (C) Johan Engelen 2007 <j.b.c.engelen@utwente.nl>
 * Copyright (C) Maximilin Albert 2008 <maximilian.albert@gmail.com>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "live_effects/lpe-mirror_symmetry.h"
#include "2geom/affine.h"
#include "2geom/path-intersection.h"
#include "display/curve.h"
#include "helper/geom.h"
#include "path-chemistry.h"
#include "style.h"
#include "svg/path-string.h"
#include "svg/svg.h"
#include <gtkmm.h>

#include "object/sp-defs.h"
#include "object/sp-lpe-item.h"
#include "object/sp-path.h"
#include "object/sp-text.h"

#include "xml/sp-css-attr.h"

// TODO due to internal breakage in glibmm headers, this must be last:
#include <glibmm/i18n.h>

namespace Inkscape {
namespace LivePathEffect {

static const Util::EnumData<ModeType> ModeTypeData[] = {
    { MT_V, N_("Vertical page center"), "vertical" },
    { MT_H, N_("Horizontal page center"), "horizontal" },
    { MT_FREE, N_("Freely defined mirror line"), "free" },
    { MT_X, N_("X coordinate of mirror line midpoint"), "X" },
    { MT_Y, N_("Y coordinate of mirror line midpoint"), "Y" }
};
static const Util::EnumDataConverter<ModeType>
MTConverter(ModeTypeData, MT_END);


LPEMirrorSymmetry::LPEMirrorSymmetry(LivePathEffectObject *lpeobject) :
    Effect(lpeobject),
    mode(_("Mode"), _("Set mode of transformation. Either freely defined by mirror line or constrained to certain symmetry points."), "mode", MTConverter, &wr, this, MT_FREE),
    discard_orig_path(_("Discard original path"), _("Only keep mirrored part of the path, remove the original."), "discard_orig_path", &wr, this, false),
    fuse_paths(_("Fuse paths"), _("Fuse original path and mirror image into a single path"), "fuse_paths", &wr, this, false),
    oposite_fuse(_("Fuse opposite sides"), _("Picks the part on the other side of the mirror line as the original."), "oposite_fuse", &wr, this, false),
    split_items(_("Split elements"), _("Split original and mirror image into separate paths, so each can have its own style."), "split_items", &wr, this, false),
    start_point(_("Mirror line start"), _("Start point of mirror line"), "start_point", &wr, this, _("Adjust start point of of mirror line")),
    end_point(_("Mirror line end"), _("End point of mirror line"), "end_point", &wr, this, _("Adjust end point of mirror line")),
    center_point(_("Mirror line mid"), _("Center point of mirror line"), "center_point", &wr, this, _("Adjust center point of mirror line"))
{
    show_orig_path = true;
    registerParameter(&mode);
    registerParameter(&discard_orig_path);
    registerParameter(&fuse_paths);
    registerParameter(&oposite_fuse);
    registerParameter(&split_items);
    registerParameter(&start_point);
    registerParameter(&end_point);
    registerParameter(&center_point);
    apply_to_clippath_and_mask = true;
    previous_center = Geom::Point(0,0);
    center_point.param_widget_is_visible(false);
    reset = false;
    center_horiz = false;
    center_vert = false;
}

LPEMirrorSymmetry::~LPEMirrorSymmetry()
= default;

void
LPEMirrorSymmetry::doAfterEffect (SPLPEItem const* lpeitem)
{
    is_load = false;
    SPDocument *document = getSPDoc();
    if (!document) {
        return;
    }
    container = dynamic_cast<SPObject *>(sp_lpe_item->parent);
    Inkscape::XML::Node *root = document->getReprRoot();

    if (split_items && !discard_orig_path) {
        Geom::Line ls((Geom::Point)start_point, (Geom::Point)end_point);
        Geom::Affine m = Geom::reflection (ls.vector(), (Geom::Point)start_point);
        m *= sp_lpe_item->transform;
        toMirror(m, reset);
        reset = false;
    } else {
        processObjects(LPE_ERASE);
        items.clear();
    }
}

Gtk::Widget *
LPEMirrorSymmetry::newWidget()
{
    // use manage here, because after deletion of Effect object, others might
    // still be pointing to this widget.
    Gtk::VBox *vbox = Gtk::manage(new Gtk::VBox(Effect::newWidget()));

    vbox->set_border_width(5);
    vbox->set_homogeneous(false);
    vbox->set_spacing(2);
    std::vector<Parameter *>::iterator it = param_vector.begin();
    while (it != param_vector.end()) {
        if ((*it)->widget_is_visible) {
            Parameter *param = *it;
            Gtk::Widget *widg = dynamic_cast<Gtk::Widget *>(param->param_newWidget());
            Glib::ustring *tip = param->param_getTooltip();
            if (widg) {
                vbox->pack_start(*widg, true, true, 2);
                if (tip) {
                    widg->set_tooltip_text(*tip);
                } else {
                    widg->set_tooltip_text("");
                    widg->set_has_tooltip(false);
                }
            }
        }

        ++it;
    }
    Gtk::HBox * hbox = Gtk::manage(new Gtk::HBox(false,0));
    Gtk::HBox * hbox2 = Gtk::manage(new Gtk::HBox(false,0));
    Gtk::Button * center_vert_button = Gtk::manage(new Gtk::Button(Glib::ustring(_("Vertical center"))));
    center_vert_button->signal_clicked().connect(sigc::mem_fun (*this,&LPEMirrorSymmetry::centerVert));
    center_vert_button->set_size_request(110,20);
    Gtk::Button * center_horiz_button = Gtk::manage(new Gtk::Button(Glib::ustring(_("Horizontal center"))));
    center_horiz_button->signal_clicked().connect(sigc::mem_fun (*this,&LPEMirrorSymmetry::centerHoriz));
    center_horiz_button->set_size_request(110,20);
    Gtk::Button * reset_button = Gtk::manage(new Gtk::Button(Glib::ustring(_("Reset styles"))));
    reset_button->signal_clicked().connect(sigc::mem_fun (*this,&LPEMirrorSymmetry::resetStyles));
    reset_button->set_size_request(110,20);
    vbox->pack_start(*hbox, true,true,2);
    vbox->pack_start(*hbox2, true,true,2);
    hbox->pack_start(*reset_button, false, false,2);
    hbox2->pack_start(*center_vert_button, false, false,2);
    hbox2->pack_start(*center_horiz_button, false, false,2);
    if(Gtk::Widget* widg = defaultParamSet()) {
        vbox->pack_start(*widg, true, true, 2);
    }
    return dynamic_cast<Gtk::Widget *>(vbox);
}

void
LPEMirrorSymmetry::centerVert(){
    center_vert = true;
    sp_lpe_item_update_patheffect(sp_lpe_item, true, false);
}

void
LPEMirrorSymmetry::centerHoriz(){
    center_horiz = true;
    sp_lpe_item_update_patheffect(sp_lpe_item, true, false);
}

void
LPEMirrorSymmetry::doBeforeEffect (SPLPEItem const* lpeitem)
{
    using namespace Geom;
    original_bbox(lpeitem, false, true);
    Point point_a(boundingbox_X.max(), boundingbox_Y.min());
    Point point_b(boundingbox_X.max(), boundingbox_Y.max());
    Point point_c(boundingbox_X.middle(), boundingbox_Y.middle());
    if (center_vert) {
        center_point.param_setValue(point_c);
        end_point.param_setValue(Geom::Point(boundingbox_X.middle(), boundingbox_Y.min()));
        //force update
        start_point.param_setValue(Geom::Point(boundingbox_X.middle(), boundingbox_Y.max()),true);
        center_vert = false;
    } else if (center_horiz) {
        center_point.param_setValue(point_c);
        end_point.param_setValue(Geom::Point(boundingbox_X.max(), boundingbox_Y.middle()));
        start_point.param_setValue(Geom::Point(boundingbox_X.min(), boundingbox_Y.middle()),true);
        //force update
        center_horiz = false;
    } else {

        if (mode == MT_Y) {
            point_a = Geom::Point(boundingbox_X.min(),center_point[Y]);
            point_b = Geom::Point(boundingbox_X.max(),center_point[Y]);
        }
        if (mode == MT_X) {
            point_a = Geom::Point(center_point[X],boundingbox_Y.min());
            point_b = Geom::Point(center_point[X],boundingbox_Y.max());
        }
        if ((Geom::Point)start_point == (Geom::Point)end_point) {
            start_point.param_setValue(point_a);
            end_point.param_setValue(point_b);
            previous_center = Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point);
            center_point.param_setValue(previous_center);
            return;
        }
        if ( mode == MT_X || mode == MT_Y ) {
            if (!are_near(previous_center, (Geom::Point)center_point, 0.01)) {
                center_point.param_setValue(Geom::middle_point(point_a, point_b));
                end_point.param_setValue(point_b);
                start_point.param_setValue(point_a);
            } else {
                if ( mode == MT_X ) {
                    if (!are_near(start_point[X], point_a[X], 0.01)) {
                        start_point.param_setValue(point_a);
                    }
                    if (!are_near(end_point[X], point_b[X], 0.01)) {
                        end_point.param_setValue(point_b);
                    }
                } else {  //MT_Y
                    if (!are_near(start_point[Y], point_a[Y], 0.01)) {
                        start_point.param_setValue(point_a);
                    }
                    if (!are_near(end_point[Y], point_b[Y], 0.01)) {
                        end_point.param_setValue(point_b);
                    }
                }
            }
        } else if ( mode == MT_FREE) {
            if (are_near(previous_center, (Geom::Point)center_point, 0.01)) {
                center_point.param_setValue(Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point));

            } else {
                Geom::Point trans = center_point - Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point);
                start_point.param_setValue(start_point * trans);
                end_point.param_setValue(end_point * trans);

            }
        } else if ( mode == MT_V){
            SPDocument *document = getSPDoc();
            if (document) {
                Geom::Affine transform = i2anc_affine(SP_OBJECT(lpeitem), nullptr).inverse();
                Geom::Point sp = Geom::Point(document->getWidth().value("px")/2.0, 0) * transform;
                start_point.param_setValue(sp);
                Geom::Point ep = Geom::Point(document->getWidth().value("px")/2.0, document->getHeight().value("px")) * transform;
                end_point.param_setValue(ep);
                center_point.param_setValue(Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point));
            }
        } else { //horizontal page
            SPDocument *document = getSPDoc();
            if (document) {
                Geom::Affine transform = i2anc_affine(SP_OBJECT(lpeitem), nullptr).inverse();
                Geom::Point sp = Geom::Point(0, document->getHeight().value("px")/2.0) * transform;
                start_point.param_setValue(sp);
                Geom::Point ep = Geom::Point(document->getWidth().value("px"), document->getHeight().value("px")/2.0) * transform;
                end_point.param_setValue(ep);
                center_point.param_setValue(Geom::middle_point((Geom::Point)start_point, (Geom::Point)end_point));
            }
        }
    }
    previous_center = center_point;
}

void LPEMirrorSymmetry::cloneStyle(SPObject *orig, SPObject *dest)
{
    dest->getRepr()->setAttribute("style", orig->getRepr()->attribute("style"));
    for (auto iter : orig->style->properties()) {
        if (iter->style_src != SP_STYLE_SRC_UNSET) {
            auto key = iter->id();
            if (key != SP_PROP_FONT && key != SP_ATTR_D && key != SP_PROP_MARKER) {
                const gchar *attr = orig->getRepr()->attribute(iter->name().c_str());
                if (attr) {
                    dest->getRepr()->setAttribute(iter->name(), attr);
                }
            }
        }
    }
}

void
LPEMirrorSymmetry::cloneD(SPObject *orig, SPObject *dest, bool reset)
{
    SPDocument *document = getSPDoc();
    if (!document) {
        return;
    }
    Inkscape::XML::Document *xml_doc = document->getReprDoc();
    if ( SP_IS_GROUP(orig) && SP_IS_GROUP(dest) && SP_GROUP(orig)->getItemCount() == SP_GROUP(dest)->getItemCount() ) {
        if (reset) {
            cloneStyle(orig, dest);
        }
        std::vector< SPObject * > childs = orig->childList(true);
        size_t index = 0;
        for (auto & child : childs) {
            SPObject *dest_child = dest->nthChild(index);
            cloneD(child, dest_child, reset);
            index++;
        }
        return;
    }

    if (SP_IS_TEXT(orig) && SP_IS_TEXT(dest) && SP_TEXT(orig)->children.size() == SP_TEXT(dest)->children.size()) {
        if (reset) {
            cloneStyle(orig, dest);
        }
        size_t index = 0;
        for (auto &child : SP_TEXT(orig)->children) {
            SPObject *dest_child = dest->nthChild(index);
            cloneD(&child, dest_child, reset);
            index++;
        }
    }

    SPShape * shape =  SP_SHAPE(orig);
    SPPath * path =  SP_PATH(dest);
    if (path && shape) {
        SPCurve *c = shape->getCurve();
        if (c) {
            gchar *str = sp_svg_write_path(c->get_pathvector());
            dest->getRepr()->setAttribute("d", str);
            g_free(str);
            c->unref();
        } else {
            dest->getRepr()->setAttribute("d", nullptr);
        }
    }
    if (reset) {
        cloneStyle(orig, dest);
    }
}

Inkscape::XML::Node *
LPEMirrorSymmetry::createPathBase(SPObject *elemref) {
    SPDocument *document = getSPDoc();
    if (!document) {
        return nullptr;
    }
    Inkscape::XML::Document *xml_doc = document->getReprDoc();
    Inkscape::XML::Node *prev = elemref->getRepr();
    SPGroup *group = dynamic_cast<SPGroup *>(elemref);
    if (group) {
        Inkscape::XML::Node *container = xml_doc->createElement("svg:g");
        container->setAttribute("transform", prev->attribute("transform"));
        std::vector<SPItem*> const item_list = sp_item_group_item_list(group);
        Inkscape::XML::Node *previous = nullptr;
        for (auto sub_item : item_list) {
            Inkscape::XML::Node *resultnode = createPathBase(sub_item);
            container->addChild(resultnode, previous);
            previous = resultnode;
        }
        return container;
    }
    Inkscape::XML::Node *resultnode = xml_doc->createElement("svg:path");
    resultnode->setAttribute("transform", prev->attribute("transform"));
    return resultnode;
}

void
LPEMirrorSymmetry::toMirror(Geom::Affine transform, bool reset)
{
    SPDocument *document = getSPDoc();
    if (!document) {
        return;
    }
    Inkscape::XML::Document *xml_doc = document->getReprDoc();
    Glib::ustring elemref_id = Glib::ustring("mirror-");
    elemref_id += this->lpeobj->getId();
    items.clear();
    items.push_back(elemref_id);
    SPObject *elemref = document->getObjectById(elemref_id.c_str());
    Inkscape::XML::Node *phantom = nullptr;
    if (elemref) {
        phantom = elemref->getRepr();
    } else {
        phantom = createPathBase(sp_lpe_item);
        phantom->setAttribute("id", elemref_id.c_str());
        reset = true;
        elemref = container->appendChildRepr(phantom);
        Inkscape::GC::release(phantom);
    }
    cloneD(SP_OBJECT(sp_lpe_item), elemref, reset);
    gchar *str = sp_svg_transform_write(transform);
    elemref->getRepr()->setAttribute("transform" , str);
    g_free(str);
    if (elemref->parent != container) {
        Inkscape::XML::Node *copy = phantom->duplicate(xml_doc);
        copy->setAttribute("id", elemref_id);
        container->appendChildRepr(copy);
        Inkscape::GC::release(copy);
        elemref->deleteObject();
    }
}


void
LPEMirrorSymmetry::resetStyles(){
    reset = true;
    doAfterEffect(sp_lpe_item);
}


//TODO: Migrate the tree next function to effect.cpp/h to avoid duplication
void
LPEMirrorSymmetry::doOnVisibilityToggled(SPLPEItem const* /*lpeitem*/)
{
    processObjects(LPE_VISIBILITY);
}

void
LPEMirrorSymmetry::doOnRemove (SPLPEItem const* /*lpeitem*/)
{
    //set "keep paths" hook on sp-lpe-item.cpp
    if (keep_paths) {
        processObjects(LPE_TO_OBJECTS);
        items.clear();
        return;
    }
    processObjects(LPE_ERASE);
}

void
LPEMirrorSymmetry::doOnApply (SPLPEItem const* lpeitem)
{
    using namespace Geom;

    original_bbox(lpeitem, false, true);

    Point point_a(boundingbox_X.max(), boundingbox_Y.min());
    Point point_b(boundingbox_X.max(), boundingbox_Y.max());
    Point point_c(boundingbox_X.max(), boundingbox_Y.middle());
    start_point.param_setValue(point_a);
    start_point.param_update_default(point_a);
    end_point.param_setValue(point_b);
    end_point.param_update_default(point_b);
    center_point.param_setValue(point_c);
    previous_center = center_point;
    SPLPEItem * splpeitem = const_cast<SPLPEItem *>(lpeitem);
}


Geom::PathVector
LPEMirrorSymmetry::doEffect_path (Geom::PathVector const & path_in)
{
    if (split_items && !fuse_paths) {
        return path_in;
    }
    Geom::PathVector const original_pathv = pathv_to_linear_and_cubic_beziers(path_in);
    Geom::PathVector path_out;

    if (!discard_orig_path && !fuse_paths) {
        path_out = pathv_to_linear_and_cubic_beziers(path_in);
    }

    Geom::Line line_separation((Geom::Point)start_point, (Geom::Point)end_point);
    Geom::Affine m = Geom::reflection (line_separation.vector(), (Geom::Point)start_point);
    if (fuse_paths && !discard_orig_path) {
        for (const auto & path_it : original_pathv)
        {
            if (path_it.empty()) {
                continue;
            }
            Geom::PathVector tmp_pathvector;
            double time_start = 0.0;
            int position = 0;
            bool end_open = false;
            if (path_it.closed()) {
                const Geom::Curve &closingline = path_it.back_closed();
                if (!are_near(closingline.initialPoint(), closingline.finalPoint())) {
                    end_open = true;
                }
            }
            Geom::Path original = path_it;
            if (end_open && path_it.closed()) {
                original.close(false);
                original.appendNew<Geom::LineSegment>( original.initialPoint() );
                original.close(true);
            }
            Geom::Point s = start_point;
            Geom::Point e = end_point;
            double dir = line_separation.angle();
            double diagonal = Geom::distance(Geom::Point(boundingbox_X.min(),boundingbox_Y.min()),Geom::Point(boundingbox_X.max(),boundingbox_Y.max()));
            Geom::Rect bbox(Geom::Point(boundingbox_X.min(),boundingbox_Y.min()),Geom::Point(boundingbox_X.max(),boundingbox_Y.max()));
            double size_divider = Geom::distance(center_point, bbox) + diagonal;
            s = Geom::Point::polar(dir,size_divider) + center_point;
            e = Geom::Point::polar(dir + Geom::rad_from_deg(180),size_divider) + center_point;
            Geom::Path divider = Geom::Path(s);
            divider.appendNew<Geom::LineSegment>(e);
            Geom::Crossings cs = crossings(original, divider);
            std::vector<double> crossed;
            for(auto & c : cs) {
                crossed.push_back(c.ta);
            }
            std::sort(crossed.begin(), crossed.end());
            for (unsigned int i = 0; i < crossed.size(); i++) {
                double time_end = crossed[i];
                if (time_start != time_end && time_end - time_start > Geom::EPSILON) {
                    Geom::Path portion = original.portion(time_start, time_end);
                    if (!portion.empty()) {
                        Geom::Point middle = portion.pointAt((double)portion.size()/2.0);
                        position = Geom::sgn(Geom::cross(e - s, middle - s));
                        if (!oposite_fuse) {
                            position *= -1;
                        }
                        if (position == 1) {
                            if (!split_items) {
                                Geom::Path mirror = portion.reversed() * m;
                                mirror.setInitial(portion.finalPoint());
                                portion.append(mirror);
                                if(i != 0) {
                                    portion.setFinal(portion.initialPoint());
                                    portion.close();
                                }
                            } else {
                                if (path_it.closed()) {
                                    portion.close();
                                }
                            }
                            tmp_pathvector.push_back(portion);
                        }
                        portion.clear();
                    }
                }
                time_start = time_end;
            }
            position = Geom::sgn(Geom::cross(e - s, original.finalPoint() - s));
            if (!oposite_fuse) {
                position *= -1;
            }
            if (cs.size()!=0 && (position == 1)) {
                if (time_start != original.size() && original.size() - time_start > Geom::EPSILON) {
                    Geom::Path portion = original.portion(time_start, original.size());
                    if (!portion.empty()) {
                        portion = portion.reversed();
                        if (!split_items) {
                            Geom::Path mirror = portion.reversed() * m;
                            mirror.setInitial(portion.finalPoint());
                            portion.append(mirror);
                        }
                        portion = portion.reversed();
                        if (!original.closed()) {
                            tmp_pathvector.push_back(portion);
                        } else {
                            if (cs.size() > 1 && tmp_pathvector.size() > 0 && tmp_pathvector[0].size() > 0 ) {
                                portion.setFinal(tmp_pathvector[0].initialPoint());
                                portion.setInitial(tmp_pathvector[0].finalPoint());
                                tmp_pathvector[0].append(portion);
                            } else {
                                tmp_pathvector.push_back(portion);
                            }
                            tmp_pathvector[0].close();
                        }
                        portion.clear();
                    }
                }
            }
            if (cs.size() == 0 && position == 1) {
                tmp_pathvector.push_back(original);
                tmp_pathvector.push_back(original * m);
            }
            path_out.insert(path_out.end(), tmp_pathvector.begin(), tmp_pathvector.end());
            tmp_pathvector.clear();
        }
    } else if (!fuse_paths || discard_orig_path) {
        for (const auto & i : original_pathv) {
            path_out.push_back(i * m);
        }
    }
    return path_out;
}

void
LPEMirrorSymmetry::addCanvasIndicators(SPLPEItem const */*lpeitem*/, std::vector<Geom::PathVector> &hp_vec)
{
    using namespace Geom;
    hp_vec.clear();
    Geom::Path path;
    Geom::Point s = start_point;
    Geom::Point e = end_point;
    path.start( s );
    path.appendNew<Geom::LineSegment>( e );
    Geom::PathVector helper;
    helper.push_back(path);
    hp_vec.push_back(helper);
}

} //namespace LivePathEffect
} /* namespace Inkscape */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
