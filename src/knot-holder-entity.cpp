// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KnotHolderEntity definition.
 *
 * Authors:
 *   Mitsuru Oka <oka326@parkcity.ne.jp>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Abhishek Sharma
 *
 * Copyright (C) 1999-2001 Lauris Kaplinski
 * Copyright (C) 2000-2001 Ximian, Inc.
 * Copyright (C) 2001 Mitsuru Oka
 * Copyright (C) 2004 Monash University
 * Copyright (C) 2008 Maximilian Albert
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "knot-holder-entity.h"

#include "desktop.h"
#include "inkscape.h"
#include "knotholder.h"
#include "preferences.h"
#include "snap.h"
#include "style.h"

#include "object/sp-hatch.h"
#include "object/sp-item.h"
#include "object/sp-namedview.h"
#include "object/sp-pattern.h"


int KnotHolderEntity::counter = 0;

void KnotHolderEntity::create(SPDesktop *desktop, SPItem *item, KnotHolder *parent, Inkscape::ControlType type,
                              const gchar *tip,
                              SPKnotShapeType shape, SPKnotModeType mode, guint32 color)
{
    if (!desktop) {
        desktop = SP_ACTIVE_DESKTOP;
    }
    knot = new SPKnot(desktop, tip);

    this->parent_holder = parent;
    this->item = item; // TODO: remove the item either from here or from knotholder.cpp
    this->desktop = desktop;

    my_counter = KnotHolderEntity::counter++;

    g_object_set(G_OBJECT(knot->item), "shape", shape, NULL);
    g_object_set(G_OBJECT(knot->item), "mode", mode, NULL);

    // TODO base more appearance from this type instead of passing in arbitrary values.
    knot->item->ctrlType = type;

    knot->fill [SP_KNOT_STATE_NORMAL] = color;
    g_object_set (G_OBJECT(knot->item), "fill_color", color, NULL);

    update_knot();
    knot->show();

    _mousedown_connection = knot->mousedown_signal.connect(sigc::mem_fun(*parent_holder, &KnotHolder::knot_mousedown_handler));
    _moved_connection = knot->moved_signal.connect(sigc::mem_fun(*parent_holder, &KnotHolder::knot_moved_handler));
    _click_connection = knot->click_signal.connect(sigc::mem_fun(*parent_holder, &KnotHolder::knot_clicked_handler));
    _ungrabbed_connection = knot->ungrabbed_signal.connect(sigc::mem_fun(*parent_holder, &KnotHolder::knot_ungrabbed_handler));
}


KnotHolderEntity::~KnotHolderEntity()
{
    _mousedown_connection.disconnect();
    _moved_connection.disconnect();
    _click_connection.disconnect();
    _ungrabbed_connection.disconnect();

    /* unref should call destroy */
    if (knot) {
        //g_object_unref(knot);
        knot_unref(knot);
    } else {
        // FIXME: This shouldn't occur. Perhaps it is caused by LPE PointParams being knotholder entities, too
        //        If so, it will likely be fixed with upcoming refactoring efforts.
        g_return_if_fail(knot);
    }
}

void
KnotHolderEntity::update_knot()
{
    Geom::Point knot_pos(knot_get());
    if (knot_pos.isFinite()) {
        Geom::Point dp(knot_pos * parent_holder->getEditTransform() * item->i2dt_affine());

        _moved_connection.block();
        knot->setPosition(dp, SP_KNOT_STATE_NORMAL);
        _moved_connection.unblock();
    } else {
        // knot coords are non-finite, hide knot
        knot->hide();
    }
}

Geom::Point
KnotHolderEntity::snap_knot_position(Geom::Point const &p, guint state)
{
    if (state & GDK_SHIFT_MASK) { // Don't snap when shift-key is held
        return p;
    }

    Geom::Affine const i2dt (parent_holder->getEditTransform() * item->i2dt_affine());
    Geom::Point s = p * i2dt;

    SnapManager &m = desktop->namedview->snap_manager;
    m.setup(desktop, true, item);
    m.freeSnapReturnByRef(s, Inkscape::SNAPSOURCE_NODE_HANDLE);
    m.unSetup();

    return s * i2dt.inverse();
}

Geom::Point
KnotHolderEntity::snap_knot_position_constrained(Geom::Point const &p, Inkscape::Snapper::SnapConstraint const &constraint, guint state)
{
    if (state & GDK_SHIFT_MASK) { // Don't snap when shift-key is held
        return p;
    }

    Geom::Affine const i2d (parent_holder->getEditTransform() * item->i2dt_affine());
    Geom::Point s = p * i2d;

    SnapManager &m = desktop->namedview->snap_manager;
    m.setup(desktop, true, item);

    // constrainedSnap() will first project the point p onto the constraint line and then try to snap along that line.
    // This way the constraint is already enforced, no need to worry about that later on
    Inkscape::Snapper::SnapConstraint transformed_constraint = Inkscape::Snapper::SnapConstraint(constraint.getPoint() * i2d, (constraint.getPoint() + constraint.getDirection()) * i2d - constraint.getPoint() * i2d);
    m.constrainedSnapReturnByRef(s, Inkscape::SNAPSOURCE_NODE_HANDLE, transformed_constraint);
    m.unSetup();

    return s * i2d.inverse();
}


/* Pattern manipulation */

/*  TODO: this pattern manipulation is not able to handle general transformation matrices. Only matrices that are the result of a pure scale times a pure rotation. */

void
PatternKnotHolderEntityXY::knot_set(Geom::Point const &p, Geom::Point const &origin, guint state)
{
    // FIXME: this snapping should be done together with knowing whether control was pressed. If GDK_CONTROL_MASK, then constrained snapping should be used.
    Geom::Point p_snapped = snap_knot_position(p, state);

    if ( state & GDK_CONTROL_MASK ) {
        if (fabs((p - origin)[Geom::X]) > fabs((p - origin)[Geom::Y])) {
            p_snapped[Geom::Y] = origin[Geom::Y];
        } else {
            p_snapped[Geom::X] = origin[Geom::X];
        }
    }

    if (state)  {
        Geom::Point const q = p_snapped - knot_get();
        item->adjust_pattern(Geom::Translate(q), false, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    }

    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

static Geom::Point sp_pattern_knot_get(SPPattern const *pat, gdouble x, gdouble y)
{
    return Geom::Point(x, y) * pat->getTransform();
}

bool
PatternKnotHolderEntity::knot_missing() const
{
    SPPattern *pat = _pattern();
    return (pat == nullptr);
}

SPPattern*
PatternKnotHolderEntity::_pattern() const
{
    return _fill ? SP_PATTERN(item->style->getFillPaintServer()) : SP_PATTERN(item->style->getStrokePaintServer());
}

Geom::Point
PatternKnotHolderEntityXY::knot_get() const
{
    SPPattern *pat = _pattern();
    return sp_pattern_knot_get(pat, 0, 0);
}

Geom::Point
PatternKnotHolderEntityAngle::knot_get() const
{
    SPPattern *pat = _pattern();
    return sp_pattern_knot_get(pat, pat->width(), 0);
}

Geom::Point
PatternKnotHolderEntityScale::knot_get() const
{
    SPPattern *pat = _pattern();
    return sp_pattern_knot_get(pat, pat->width(), pat->height());
}

void
PatternKnotHolderEntityAngle::knot_set(Geom::Point const &p, Geom::Point const &/*origin*/, guint state)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int const snaps = prefs->getInt("/options/rotationsnapsperpi/value", 12);

    SPPattern *pat = _fill ? SP_PATTERN(item->style->getFillPaintServer()) : SP_PATTERN(item->style->getStrokePaintServer());

    // get the angle from pattern 0,0 to the cursor pos
    Geom::Point transform_origin = sp_pattern_knot_get(pat, 0, 0);
    gdouble theta = atan2(p - transform_origin);
    gdouble theta_old = atan2(knot_get() - transform_origin);

    if ( state & GDK_CONTROL_MASK ) {
        /* Snap theta */
        double snaps_radian = M_PI/snaps;
        theta = std::round(theta/snaps_radian) * snaps_radian;
    }

    Geom::Affine rot = Geom::Translate(-transform_origin)
                     * Geom::Rotate(theta - theta_old)
                     * Geom::Translate(transform_origin);
    item->adjust_pattern(rot, false, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

void
PatternKnotHolderEntityScale::knot_set(Geom::Point const &p, Geom::Point const &origin, guint state)
{
    SPPattern *pat = _pattern();

    // FIXME: this snapping should be done together with knowing whether control was pressed. If GDK_CONTROL_MASK, then constrained snapping should be used.
    Geom::Point p_snapped = snap_knot_position(p, state);

    // Get the new scale from the position of the knotholder
    Geom::Affine transform = pat->getTransform();
    Geom::Affine transform_inverse = transform.inverse();
    Geom::Point d = p_snapped * transform_inverse;
    Geom::Point d_origin = origin * transform_inverse;
    Geom::Point origin_dt;
    gdouble pat_x = pat->width();
    gdouble pat_y = pat->height();
    if ( state & GDK_CONTROL_MASK ) {
        // if ctrl is pressed: use 1:1 scaling
        d = d_origin * (d.length() / d_origin.length());
    }

    Geom::Affine rot = Geom::Translate(-origin_dt)
                     * Geom::Scale(d.x() / pat_x, d.y() / pat_y)
                     * Geom::Translate(origin_dt)
                     * transform;

    item->adjust_pattern(rot, true, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

/* Hatch manipulation */
bool HatchKnotHolderEntity::knot_missing() const
{
    SPHatch *hatch = _hatch();
    return (hatch == nullptr);
}

SPHatch *HatchKnotHolderEntity::_hatch() const
{
    return _fill ? SP_HATCH(item->style->getFillPaintServer()) : SP_HATCH(item->style->getStrokePaintServer());
}

static Geom::Point sp_hatch_knot_get(SPHatch const *hatch, gdouble x, gdouble y)
{
    return Geom::Point(x, y) * hatch->hatchTransform();
}

Geom::Point HatchKnotHolderEntityXY::knot_get() const
{
    SPHatch *hatch = _hatch();
    return sp_hatch_knot_get(hatch, 0, 0);
}

Geom::Point HatchKnotHolderEntityAngle::knot_get() const
{
    SPHatch *hatch = _hatch();
    return sp_hatch_knot_get(hatch, hatch->pitch(), 0);
}

Geom::Point HatchKnotHolderEntityScale::knot_get() const
{
    SPHatch *hatch = _hatch();
    return sp_hatch_knot_get(hatch, hatch->pitch(), hatch->pitch());
}

void HatchKnotHolderEntityXY::knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state)
{
    Geom::Point p_snapped = snap_knot_position(p, state);

    if (state & GDK_CONTROL_MASK) {
        if (fabs((p - origin)[Geom::X]) > fabs((p - origin)[Geom::Y])) {
            p_snapped[Geom::Y] = origin[Geom::Y];
        } else {
            p_snapped[Geom::X] = origin[Geom::X];
        }
    }

    if (state) {
        Geom::Point const q = p_snapped - knot_get();
        item->adjust_hatch(Geom::Translate(q), false, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    }

    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

void HatchKnotHolderEntityAngle::knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int const snaps = prefs->getInt("/options/rotationsnapsperpi/value", 12);

    SPHatch *hatch = _hatch();

    // get the angle from hatch 0,0 to the cursor pos
    Geom::Point transform_origin = sp_hatch_knot_get(hatch, 0, 0);
    gdouble theta = atan2(p - transform_origin);
    gdouble theta_old = atan2(knot_get() - transform_origin);

    if (state & GDK_CONTROL_MASK) {
        /* Snap theta */
        double snaps_radian = M_PI/snaps;
        theta = std::round(theta/snaps_radian) * snaps_radian;
    }

    Geom::Affine rot =
        Geom::Translate(-transform_origin) * Geom::Rotate(theta - theta_old) * Geom::Translate(transform_origin);
    item->adjust_hatch(rot, false, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

void HatchKnotHolderEntityScale::knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state)
{
    SPHatch *hatch = _hatch();

    // FIXME: this snapping should be done together with knowing whether control was pressed.
    // If GDK_CONTROL_MASK, then constrained snapping should be used.
    Geom::Point p_snapped = snap_knot_position(p, state);

    // Get the new scale from the position of the knotholder
    Geom::Affine transform = hatch->hatchTransform();
    Geom::Affine transform_inverse = transform.inverse();
    Geom::Point d = p_snapped * transform_inverse;
    Geom::Point d_origin = origin * transform_inverse;
    Geom::Point origin_dt;
    gdouble hatch_pitch = hatch->pitch();
    if (state & GDK_CONTROL_MASK) {
        // if ctrl is pressed: use 1:1 scaling
        d = d_origin * (d.length() / d_origin.length());
    }

    Geom::Affine scale = Geom::Translate(-origin_dt) * Geom::Scale(d.x() / hatch_pitch, d.y() / hatch_pitch) *
                         Geom::Translate(origin_dt) * transform;

    item->adjust_hatch(scale, true, _fill ? TRANSFORM_FILL : TRANSFORM_STROKE);
    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

/* Filter manipulation */
void FilterKnotHolderEntity::knot_set(Geom::Point const &p, Geom::Point const &origin, unsigned int state)
{
    // FIXME: this snapping should be done together with knowing whether control was pressed. If GDK_CONTROL_MASK, then constrained snapping should be used.
    Geom::Point p_snapped = snap_knot_position(p, state);

    if ( state & GDK_CONTROL_MASK ) {
        if (fabs((p - origin)[Geom::X]) > fabs((p - origin)[Geom::Y])) {
            p_snapped[Geom::Y] = origin[Geom::Y];
        } else {
            p_snapped[Geom::X] = origin[Geom::X];
        }
    }

    if (state)  {
        SPFilter *filter = (item->style && item->style->filter.href) ? dynamic_cast<SPFilter *>(item->style->getFilter()) : nullptr;
        if(!filter) return;
        Geom::OptRect orig_bbox = item->visualBounds();
        std::unique_ptr<Geom::Rect> new_bbox(_topleft ? new Geom::Rect(p,orig_bbox->max()) : new Geom::Rect(orig_bbox->min(), p));
        if(_topleft) {
            float x_a = filter->width.computed;
            float y_a = filter->height.computed;
            filter->height.scale(new_bbox->height()/orig_bbox->height());
            filter->width.scale(new_bbox->width()/orig_bbox->width());
            float x_b = filter->width.computed;
            float y_b = filter->height.computed;
            filter->x.set(filter->x.unit, filter->x.computed + x_a - x_b);
            filter->y.set(filter->y.unit, filter->y.computed + y_a - y_b);
        } else {
            filter->height.scale(new_bbox->height()/orig_bbox->height());
            filter->width.scale(new_bbox->width()/orig_bbox->width());
        }
        filter->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);

        //filter->

        //item-> //adjust FER
    }

    item->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
}

Geom::Point FilterKnotHolderEntity::knot_get() const
{
    SPFilter *filter = (item->style && item->style->filter.href) ? dynamic_cast<SPFilter *>(item->style->getFilter()) : nullptr;
    if(!filter) return Geom::Point(Geom::infinity(), Geom::infinity());
    Geom::OptRect r = item->visualBounds();
    if (_topleft) return Geom::Point(r->min());
    else return Geom::Point(r->max());
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
