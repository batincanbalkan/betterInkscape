// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef INKSCAPE_LIVEPATHEFFECT_PARAMETER_TRANSFORMED_POINT_H
#define INKSCAPE_LIVEPATHEFFECT_PARAMETER_TRANSFORMED_POINT_H

/*
 * Inkscape::LivePathEffectParameters
 *
 * Copyright (C) Theodore Janeczko 2012 <flutterguy317@gmail.com>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <glib.h>
#include <2geom/point.h>

#include "live_effects/parameter/parameter.h"

#include "knot-holder-entity.h"

namespace Inkscape {

namespace LivePathEffect {


class TransformedPointParam : public Parameter {
public:
    TransformedPointParam( const Glib::ustring& label,
                const Glib::ustring& tip,
                const Glib::ustring& key,
                Inkscape::UI::Widget::Registry* wr,
                Effect* effect,
                Geom::Point default_vector = Geom::Point(1,0),
                bool dontTransform = false);
    ~TransformedPointParam() override;

    Gtk::Widget * param_newWidget() override;
    inline const gchar *handleTip() const { return param_tooltip.c_str(); }

    bool param_readSVGValue(const gchar * strvalue) override;
    gchar * param_getSVGValue() const override;
    gchar * param_getDefaultSVGValue() const override;

    Geom::Point getVector() const { return vector; };
    Geom::Point getOrigin() const { return origin; };
    void setValues(Geom::Point const &new_origin, Geom::Point const &new_vector) { setVector(new_vector); setOrigin(new_origin); };
    void setVector(Geom::Point const &new_vector) { vector = new_vector; };
    void setOrigin(Geom::Point const &new_origin) { origin = new_origin; };
    void param_set_default() override;

    void set_and_write_new_values(Geom::Point const &new_origin, Geom::Point const &new_vector);

    void param_transform_multiply(Geom::Affine const &postmul, bool set) override;

    void set_vector_oncanvas_looks(SPKnotShapeType shape, SPKnotModeType mode, guint32 color);
    void set_oncanvas_color(guint32 color);
    Geom::Point param_get_default() { return defvalue; }
    void param_update_default(Geom::Point default_point);
    void param_update_default(const gchar * default_point) override;
    bool providesKnotHolderEntities() const override { return true; }
    virtual void addKnotHolderEntities(KnotHolder *knotholder, SPDesktop *desktop, SPItem *item);

private:
    TransformedPointParam(const TransformedPointParam&) = delete;
    TransformedPointParam& operator=(const TransformedPointParam&) = delete;

    Geom::Point defvalue;

    Geom::Point origin;
    Geom::Point vector;

    bool noTransform;
    
    /// The looks of the vector and origin knots oncanvas
    SPKnotShapeType vec_knot_shape;
    SPKnotModeType  vec_knot_mode;
    guint32         vec_knot_color;

    friend class TransformedPointParamKnotHolderEntity_Vector;
};


} //namespace LivePathEffect

} //namespace Inkscape

#endif
