// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */
#include "live_effects/lpe-powerclip.h"
#include "display/curve.h"
#include "live_effects/lpeobject-reference.h"
#include "live_effects/lpeobject.h"
#include "object/sp-clippath.h"
#include "object/sp-defs.h"
#include "object/sp-item-group.h"
#include "object/sp-item.h"
#include "object/sp-path.h"
#include "object/sp-shape.h"
#include "object/sp-use.h"
#include "style.h"
#include "svg/svg.h"

#include <2geom/intersection-graph.h>
#include <2geom/path-intersection.h>
// TODO due to internal breakage in glibmm headers, this must be last:
#include <glibmm/i18n.h>

namespace Inkscape {
namespace LivePathEffect {

LPEPowerClip::LPEPowerClip(LivePathEffectObject *lpeobject)
    : Effect(lpeobject)
    , hide_clip(_("Hide clip"), _("Hide clip"), "hide_clip", &wr, this, false)
    , inverse(_("Inverse clip"), _("Inverse clip"), "inverse", &wr, this, true)
    , flatten(_("Flatten clip"), _("Flatten clip, see fill rule once convert to paths"), "flatten", &wr, this, false)
    , message(
          _("Info Box"), _("Important messages"), "message", &wr, this,
          _("Use fill-rule evenodd on <b>fill and stroke</b> dialog if no flatten result after convert clip to paths."))
{
    registerParameter(&inverse);
    registerParameter(&flatten);
    registerParameter(&hide_clip);
    registerParameter(&message);
    message.param_set_min_height(55);
    _updating = false;
    _legacy = false;
    // legazy fix between 0.92.4 launch and 1.0beta1
    if (this->getRepr()->attribute("is_inverse")) {
        this->getRepr()->setAttribute("is_inverse", nullptr);
        _legacy = true;
    }
}

LPEPowerClip::~LPEPowerClip() = default;

Geom::Path sp_bbox_without_clip(SPLPEItem *lpeitem)
{
    Geom::OptRect bbox = lpeitem->visualBounds(Geom::identity(), true, false, true);
    if (bbox) {
        (*bbox).expandBy(5);
        return Geom::Path(*bbox);
    }
    return Geom::Path();
}

Geom::PathVector sp_get_recursive_pathvector(SPLPEItem *item, Geom::PathVector res, bool dir, bool inverse)
{
    SPGroup *group = dynamic_cast<SPGroup *>(item);
    if (group) {
        std::vector<SPItem *> item_list = sp_item_group_item_list(group);
        for (auto child : item_list) {
            if (child) {
                SPLPEItem *childitem = dynamic_cast<SPLPEItem *>(child);
                if (childitem) {
                    res = sp_get_recursive_pathvector(childitem, res, dir, inverse);
                }
            }
        }
    }
    SPShape *shape = dynamic_cast<SPShape *>(item);
    if (shape && shape->getCurve()) {
        for (auto path : shape->getCurve()->get_pathvector()) {
            if (!path.empty()) {
                bool pathdir = Geom::path_direction(path);
                if (pathdir == dir && inverse) {
                    path = path.reversed();
                }
                res.push_back(path);
            }
        }
    }
    return res;
}

Geom::PathVector LPEPowerClip::getClipPathvector()
{
    Geom::PathVector res;
    Geom::PathVector res_hlp;
    if (!sp_lpe_item) {
        return res;
    }

    SPObject *clip_path = sp_lpe_item->clip_ref->getObject();
    if (clip_path) {
        std::vector<SPObject*> clip_path_list = clip_path->childList(true);
        clip_path_list.pop_back();
        if (clip_path_list.size()) {
            for (auto clip : clip_path_list) {
                SPLPEItem *childitem = dynamic_cast<SPLPEItem *>(clip);
                if (childitem) {
                    res_hlp = sp_get_recursive_pathvector(childitem, res_hlp, false, inverse);
                    if (is_load && _legacy) {
                        childitem->doWriteTransform(Geom::Translate(0, -999999));
                    }
                    if (!childitem->style || !childitem->style->display.set ||
                        childitem->style->display.value != SP_CSS_DISPLAY_NONE) {
                        childitem->style->display.set = TRUE;
                        childitem->style->display.value = SP_CSS_DISPLAY_NONE;
                        childitem->updateRepr(SP_OBJECT_WRITE_NO_CHILDREN | SP_OBJECT_WRITE_EXT);
                    }
                }
            }
            if (is_load && _legacy) {
                res_hlp *= Geom::Translate(0, -999999);
                _legacy = false;
            }
        }
    }
    Geom::Path bbox = sp_bbox_without_clip(sp_lpe_item);
    if (hide_clip) {
        return bbox;
    }
    if (inverse && isVisible()) {
        res.push_back(bbox);
    }
    for (auto path : res_hlp) {
        res.push_back(path);
    }
    return res;
}

void LPEPowerClip::add()
{
    SPDocument *document = getSPDoc();
    if (!document || !sp_lpe_item) {
        return;
    }
    SPObject *clip_path = sp_lpe_item->clip_ref->getObject();
    SPObject *elemref = NULL;
    if (clip_path) {
        Inkscape::XML::Document *xml_doc = document->getReprDoc();
        Inkscape::XML::Node *parent = clip_path->getRepr();
        SPLPEItem *childitem = dynamic_cast<SPLPEItem *>(clip_path->childList(true).back());
        if (childitem) {
            if (const gchar *powerclip = childitem->getRepr()->attribute("class")) {
                if (!strcmp(powerclip, "powerclip")) {
                    Glib::ustring newclip = Glib::ustring("clipath_") + getId();
                    Glib::ustring uri = Glib::ustring("url(#") + newclip + Glib::ustring(")");
                    parent = clip_path->getRepr()->duplicate(xml_doc);
                    parent->setAttribute("id", newclip.c_str());
                    Inkscape::XML::Node *defs = clip_path->getRepr()->parent();
                    clip_path = SP_OBJECT(document->getDefs()->appendChildRepr(parent));
                    Inkscape::GC::release(parent);
                    sp_lpe_item->setAttribute("clip-path", uri.c_str());
                    SPLPEItem *childitemdel = dynamic_cast<SPLPEItem *>(clip_path->childList(true).back());
                    if (childitemdel) {
                        childitemdel->setAttribute("id", getId().c_str());
                        return;
                    }
                }
            }
        }
        Inkscape::XML::Node *clip_path_node = xml_doc->createElement("svg:path");
        parent->appendChild(clip_path_node);
        Inkscape::GC::release(clip_path_node);
        elemref = document->getObjectByRepr(clip_path_node);
        if (elemref) {
            elemref->setAttribute("style", "fill-rule:evenodd");
            elemref->setAttribute("class", "powerclip");
            elemref->setAttribute("id", getId().c_str());
            gchar *str = sp_svg_write_path(getClipPathvector());
            elemref->setAttribute("d", str);
            g_free(str);
        } else {
            sp_lpe_item->removeCurrentPathEffect(false);
        }
    } else {
        sp_lpe_item->removeCurrentPathEffect(false);
    }
}

Glib::ustring LPEPowerClip::getId() { return Glib::ustring("lpe_") + Glib::ustring(getLPEObj()->getId()); }

void LPEPowerClip::upd()
{
    SPDocument *document = getSPDoc();
    if (!document || !sp_lpe_item) {
        return;
    }
    SPObject *elemref = document->getObjectById(getId().c_str());
    if (elemref && sp_lpe_item) {
        gchar *str = sp_svg_write_path(getClipPathvector());
        elemref->setAttribute("d", str);
        g_free(str);
        elemref->updateRepr(SP_OBJECT_WRITE_NO_CHILDREN | SP_OBJECT_WRITE_EXT);
    } else {
        add();
    }
}


void LPEPowerClip::doBeforeEffect(SPLPEItem const *lpeitem)
{
    if (!_updating) {
        upd();
    }
}

void 
LPEPowerClip::doOnRemove (SPLPEItem const* /*lpeitem*/)
{
    SPDocument *document = getSPDoc();
    if (!document) {
        return;
    }
    _updating = true;
    SPObject *elemref = document->getObjectById(getId().c_str());
    if (elemref) {
        elemref->deleteObject();
    }
    SPObject *clip_path = sp_lpe_item->clip_ref->getObject();
    if (clip_path) {
        std::vector<SPObject *> clip_path_list = clip_path->childList(true);
        for (auto clip : clip_path_list) {
            SPLPEItem *childitem = dynamic_cast<SPLPEItem *>(clip);
            if (childitem) {
                if (!childitem->style || childitem->style->display.set ||
                    childitem->style->display.value == SP_CSS_DISPLAY_NONE) {
                    childitem->style->display.set = TRUE;
                    childitem->style->display.value = SP_CSS_DISPLAY_BLOCK;
                    childitem->updateRepr(SP_OBJECT_WRITE_NO_CHILDREN | SP_OBJECT_WRITE_EXT);
                }
            }
        }
    }
}

Geom::PathVector
LPEPowerClip::doEffect_path(Geom::PathVector const & path_in){
    Geom::PathVector path_out = path_in;
    if (flatten) {
        Geom::PathVector c_pv = getClipPathvector();
        std::unique_ptr<Geom::PathIntersectionGraph> pig(new Geom::PathIntersectionGraph(c_pv, path_out));
        if (pig && !c_pv.empty() && !path_out.empty()) {
            path_out = pig->getIntersection();
        }
    }
    return path_out;
}

void LPEPowerClip::doOnVisibilityToggled(SPLPEItem const *lpeitem) { upd(); }

void sp_remove_powerclip(Inkscape::Selection *sel)
{
    if (!sel->isEmpty()) {
        auto selList = sel->items();
        for (auto i = boost::rbegin(selList); i != boost::rend(selList); ++i) {
            SPLPEItem *lpeitem = dynamic_cast<SPLPEItem *>(*i);
            if (lpeitem) {
                if (lpeitem->hasPathEffect() && lpeitem->pathEffectsEnabled()) {
                    for (PathEffectList::iterator it = lpeitem->path_effect_list->begin();
                         it != lpeitem->path_effect_list->end(); ++it) {
                        LivePathEffectObject *lpeobj = (*it)->lpeobject;
                        if (!lpeobj) {
                            /** \todo Investigate the cause of this.
                             * For example, this happens when copy pasting an object with LPE applied. Probably because
                             * the object is pasted while the effect is not yet pasted to defs, and cannot be found.
                             */
                            g_warning("SPLPEItem::performPathEffect - NULL lpeobj in list!");
                            return;
                        }
                        Inkscape::LivePathEffect::Effect *lpe = lpeobj->get_lpe();
                        if (lpe->getName() == "Power clip") {
                            lpeitem->setCurrentPathEffect(*it);
                            lpeitem->removeCurrentPathEffect(false);
                            break;
                        }
                    }
                }
            }
        }
    }
}

void sp_inverse_powerclip(Inkscape::Selection *sel) {
    if (!sel->isEmpty()) {
        auto selList = sel->items();
        for(auto i = boost::rbegin(selList); i != boost::rend(selList); ++i) {
            SPLPEItem* lpeitem = dynamic_cast<SPLPEItem*>(*i);
            if (lpeitem) {
                SPClipPath *clip_path = SP_ITEM(lpeitem)->clip_ref->getObject();
                if(clip_path) {
                    std::vector<SPObject*> clip_path_list = clip_path->childList(true);
                    for (std::vector<SPObject*>::const_iterator iter=clip_path_list.begin();iter!=clip_path_list.end();++iter) {
                        SPUse *use = dynamic_cast<SPUse*>(*iter);
                        if (use) {
                            g_warning("We can`t add inverse clip on clones");
                            return;
                        }
                    }
                    Effect::createAndApply(POWERCLIP, SP_ACTIVE_DOCUMENT, lpeitem);
                    Effect* lpe = lpeitem->getCurrentLPE();
                    lpe->getRepr()->setAttribute("inverse", "true");
                }
            }
        }
    }
}

}; //namespace LivePathEffect
}; /* namespace Inkscape */

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
