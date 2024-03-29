// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SP_LPE_ITEM_H_SEEN
#define SP_LPE_ITEM_H_SEEN

/** \file
 * Base class for live path effect items
 */
/*
 * Authors:
 *   Johan Engelen <j.b.c.engelen@ewi.utwente.nl>
 *   Bastien Bouclet <bgkweb@gmail.com>
 *
 * Copyright (C) 2008 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <list>
#include <string>
#include "sp-item.h"

#define SP_LPE_ITEM(obj) (dynamic_cast<SPLPEItem*>((SPObject*)obj))
#define SP_IS_LPE_ITEM(obj) (dynamic_cast<const SPLPEItem*>((SPObject*)obj) != NULL)

class LivePathEffectObject;
class SPCurve;
class SPShape;
class SPDesktop;

namespace Inkscape{ 
    namespace Display {
        class TemporaryItem;
    }
    namespace LivePathEffect{
        class LPEObjectReference;
        class Effect;
    }
}

typedef std::list<Inkscape::LivePathEffect::LPEObjectReference *> PathEffectList;

class SPLPEItem : public SPItem {
public:
    SPLPEItem();
    ~SPLPEItem() override;

    int path_effects_enabled;

    PathEffectList* path_effect_list;
    std::list<sigc::connection> *lpe_modified_connection_list; // this list contains the connections for listening to lpeobject parameter changes

    Inkscape::LivePathEffect::LPEObjectReference* current_path_effect;
    std::vector<Inkscape::Display::TemporaryItem*> lpe_helperpaths;

    void replacePathEffects( std::vector<LivePathEffectObject const *> const &old_lpeobjs,
                             std::vector<LivePathEffectObject const *> const &new_lpeobjs );


    void build(SPDocument* doc, Inkscape::XML::Node* repr) override;
    void release() override;

    void set(SPAttributeEnum key, char const* value) override;

    void update(SPCtx* ctx, unsigned int flags) override;
    void modified(unsigned int flags) override;

    void child_added(Inkscape::XML::Node* child, Inkscape::XML::Node* ref) override;
    void remove_child(Inkscape::XML::Node* child) override;

    Inkscape::XML::Node* write(Inkscape::XML::Document *xml_doc, Inkscape::XML::Node *repr, unsigned int flags) override;

    virtual void update_patheffect(bool write);

    bool performPathEffect(SPCurve *curve, SPShape *current, bool is_clip_or_mask = false);
    bool performOnePathEffect(SPCurve *curve, SPShape *current, Inkscape::LivePathEffect::Effect *lpe, bool is_clip_or_mask = false);
    bool pathEffectsEnabled() const;
    bool hasPathEffect() const;
    bool hasPathEffectOfType(int const type, bool is_ready = true) const;
    bool hasPathEffectRecursive() const;
    bool hasPathEffectOnClipOrMask(SPLPEItem * shape) const;
    bool hasPathEffectOnClipOrMaskRecursive(SPLPEItem * shape) const;
    Inkscape::LivePathEffect::Effect* getPathEffectOfType(int type);
    Inkscape::LivePathEffect::Effect const* getPathEffectOfType(int type) const;
    bool hasBrokenPathEffect() const;

    PathEffectList getEffectList();
    PathEffectList const getEffectList() const;

    void downCurrentPathEffect();
    void upCurrentPathEffect();
    Inkscape::LivePathEffect::LPEObjectReference* getCurrentLPEReference();
    Inkscape::LivePathEffect::Effect* getCurrentLPE();
    bool setCurrentPathEffect(Inkscape::LivePathEffect::LPEObjectReference* lperef);
    void removeCurrentPathEffect(bool keep_paths);
    void removeAllPathEffects(bool keep_paths);
    void addPathEffect(std::string value, bool reset);
    void addPathEffect(LivePathEffectObject * new_lpeobj);
    void resetClipPathAndMaskLPE(bool fromrecurse = false);
    void applyToMask(SPItem* to, Inkscape::LivePathEffect::Effect *lpe = nullptr);
    void applyToClipPath(SPItem* to, Inkscape::LivePathEffect::Effect *lpe = nullptr);
    void applyToClipPathOrMask(SPItem * clip_mask, SPItem* to, Inkscape::LivePathEffect::Effect *lpe = nullptr);
    bool forkPathEffectsIfNecessary(unsigned int nr_of_allowed_users = 1, bool recursive = true);

    void editNextParamOncanvas(SPDesktop *dt);
};
void sp_lpe_item_update_patheffect (SPLPEItem *lpeitem, bool wholetree, bool write); // careful, class already has method with *very* similar name!
void sp_lpe_item_enable_path_effects(SPLPEItem *lpeitem, bool enable);

#endif /* !SP_LPE_ITEM_H_SEEN */

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
