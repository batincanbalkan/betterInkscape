// SPDX-License-Identifier: GPL-2.0-or-later
/** \file
 * SVG <feDiffuseLighting> implementation.
 *
 */
/*
 * Authors:
 *   hugo Rodrigues <haa.rodrigues@gmail.com>
 *   Jean-Rene Reinhard <jr@komite.net>
 *   Abhishek Sharma
 *
 * Copyright (C) 2006 Hugo Rodrigues
 *               2007 authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

// Same directory
#include "diffuselighting.h"
#include "distantlight.h"
#include "pointlight.h"
#include "spotlight.h"

#include "strneq.h"
#include "attributes.h"

#include "display/nr-filter.h"
#include "display/nr-filter-diffuselighting.h"

#include "svg/svg.h"
#include "svg/svg-color.h"
#include "svg/svg-icc-color.h"

#include "xml/repr.h"

/* FeDiffuseLighting base class */
static void sp_feDiffuseLighting_children_modified(SPFeDiffuseLighting *sp_diffuselighting);

SPFeDiffuseLighting::SPFeDiffuseLighting() : SPFilterPrimitive() {
    this->surfaceScale = 1;
    this->diffuseConstant = 1;
    this->lighting_color = 0xffffffff;
    this->icc = nullptr;

    //TODO kernelUnit
    this->renderer = nullptr;

    this->surfaceScale_set = FALSE;
    this->diffuseConstant_set = FALSE;
    this->lighting_color_set = FALSE;
}

SPFeDiffuseLighting::~SPFeDiffuseLighting() = default;

/**
 * Reads the Inkscape::XML::Node, and initializes SPFeDiffuseLighting variables.  For this to get called,
 * our name must be associated with a repr via "sp_object_type_register".  Best done through
 * sp-object-repr.cpp's repr_name_entries array.
 */
void SPFeDiffuseLighting::build(SPDocument *document, Inkscape::XML::Node *repr) {
	SPFilterPrimitive::build(document, repr);

	/*LOAD ATTRIBUTES FROM REPR HERE*/
	this->readAttr( "surfaceScale" );
	this->readAttr( "diffuseConstant" );
	this->readAttr( "kernelUnitLength" );
	this->readAttr( "lighting-color" );
}

/**
 * Drops any allocated memory.
 */
void SPFeDiffuseLighting::release() {
	SPFilterPrimitive::release();
}

/**
 * Sets a specific value in the SPFeDiffuseLighting.
 */
void SPFeDiffuseLighting::set(SPAttributeEnum key, gchar const *value) {
    gchar const *cend_ptr = nullptr;
    gchar *end_ptr = nullptr;
    
    switch(key) {
	/*DEAL WITH SETTING ATTRIBUTES HERE*/
    //TODO test forbidden values
        case SP_ATTR_SURFACESCALE:
            end_ptr = nullptr;

            if (value) {
                this->surfaceScale = g_ascii_strtod(value, &end_ptr);

                if (end_ptr) {
                    this->surfaceScale_set = TRUE;
                }
            } 

            if (!value || !end_ptr) {
                this->surfaceScale = 1;
                this->surfaceScale_set = FALSE;
            }

            if (this->renderer) {
                this->renderer->surfaceScale = this->surfaceScale;
            }

            this->parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
        case SP_ATTR_DIFFUSECONSTANT:
            end_ptr = nullptr;

            if (value) {
                this->diffuseConstant = g_ascii_strtod(value, &end_ptr);

                if (end_ptr && this->diffuseConstant >= 0) {
                    this->diffuseConstant_set = TRUE;
                } else {
                    end_ptr = nullptr;
                    g_warning("this: diffuseConstant should be a positive number ... defaulting to 1");
                }
            } 

            if (!value || !end_ptr) {
                this->diffuseConstant = 1;
                this->diffuseConstant_set = FALSE;
            }

            if (this->renderer) {
                this->renderer->diffuseConstant = this->diffuseConstant;
            }

            this->parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
        case SP_ATTR_KERNELUNITLENGTH:
            //TODO kernelUnit
            //this->kernelUnitLength.set(value);
            /*TODOif (feDiffuseLighting->renderer) {
                feDiffuseLighting->renderer->surfaceScale = feDiffuseLighting->renderer;
            }
            */
            this->parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
        case SP_PROP_LIGHTING_COLOR:
            cend_ptr = nullptr;
            this->lighting_color = sp_svg_read_color(value, &cend_ptr, 0xffffffff);

            //if a value was read
            if (cend_ptr) {
                while (g_ascii_isspace(*cend_ptr)) {
                    ++cend_ptr;
                }

                if (strneq(cend_ptr, "icc-color(", 10)) {
                    if (!this->icc) {
                    	this->icc = new SVGICCColor();
                    }

                    if ( ! sp_svg_read_icc_color( cend_ptr, this->icc ) ) {
                        delete this->icc;
                        this->icc = nullptr;
                    }
                }

                this->lighting_color_set = TRUE;
            } else {
                //lighting_color already contains the default value
                this->lighting_color_set = FALSE;
            }

            if (this->renderer) {
                this->renderer->lighting_color = this->lighting_color;
            }

            this->parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
            break;
        default:
        	SPFilterPrimitive::set(key, value);
            break;
    }
}

/**
 * Receives update notifications.
 */
void SPFeDiffuseLighting::update(SPCtx *ctx, guint flags) {
    if (flags & (SP_OBJECT_MODIFIED_FLAG)) {
        this->readAttr( "surfaceScale" );
        this->readAttr( "diffuseConstant" );
        this->readAttr( "kernelUnit" );
        this->readAttr( "lighting-color" );
    }

    SPFilterPrimitive::update(ctx, flags);
}

/**
 * Writes its settings to an incoming repr object, if any.
 */
Inkscape::XML::Node* SPFeDiffuseLighting::write(Inkscape::XML::Document *doc, Inkscape::XML::Node *repr, guint flags) {
    /* TODO: Don't just clone, but create a new repr node and write all
     * relevant values _and children_ into it */
    if (!repr) {
        repr = this->getRepr()->duplicate(doc);
        //repr = doc->createElement("svg:feDiffuseLighting");
    }
    
    if (this->surfaceScale_set) {
        sp_repr_set_css_double(repr, "surfaceScale", this->surfaceScale);
    } else {
        repr->setAttribute("surfaceScale", nullptr);
    }

    if (this->diffuseConstant_set) {
        sp_repr_set_css_double(repr, "diffuseConstant", this->diffuseConstant);
    } else {
        repr->setAttribute("diffuseConstant", nullptr);
    }

    /*TODO kernelUnits */
    if (this->lighting_color_set) {
        gchar c[64];
        sp_svg_write_color(c, sizeof(c), this->lighting_color);
        repr->setAttribute("lighting-color", c);
    } else {
        repr->setAttribute("lighting-color", nullptr);
    }
        
    SPFilterPrimitive::write(doc, repr, flags);

    return repr;
}

/**
 * Callback for child_added event.
 */
void SPFeDiffuseLighting::child_added(Inkscape::XML::Node *child, Inkscape::XML::Node *ref) {
    SPFilterPrimitive::child_added(child, ref);

    sp_feDiffuseLighting_children_modified(this);
    this->parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
}

/**
 * Callback for remove_child event.
 */
void SPFeDiffuseLighting::remove_child(Inkscape::XML::Node *child) {
	SPFilterPrimitive::remove_child(child);

	sp_feDiffuseLighting_children_modified(this);
	this->parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
}

void SPFeDiffuseLighting::order_changed(Inkscape::XML::Node *child, Inkscape::XML::Node *old_ref, Inkscape::XML::Node *new_ref) {
    SPFilterPrimitive::order_changed(child, old_ref, new_ref);

    sp_feDiffuseLighting_children_modified(this);
    this->parent->requestModified(SP_OBJECT_MODIFIED_FLAG);
}

static void sp_feDiffuseLighting_children_modified(SPFeDiffuseLighting *sp_diffuselighting)
{
   if (sp_diffuselighting->renderer) {
        sp_diffuselighting->renderer->light_type = Inkscape::Filters::NO_LIGHT;
        if (SP_IS_FEDISTANTLIGHT(sp_diffuselighting->firstChild())) {
            sp_diffuselighting->renderer->light_type = Inkscape::Filters::DISTANT_LIGHT;
            sp_diffuselighting->renderer->light.distant = SP_FEDISTANTLIGHT(sp_diffuselighting->firstChild());
        }
        if (SP_IS_FEPOINTLIGHT(sp_diffuselighting->firstChild())) {
            sp_diffuselighting->renderer->light_type = Inkscape::Filters::POINT_LIGHT;
            sp_diffuselighting->renderer->light.point = SP_FEPOINTLIGHT(sp_diffuselighting->firstChild());
        }
        if (SP_IS_FESPOTLIGHT(sp_diffuselighting->firstChild())) {
            sp_diffuselighting->renderer->light_type = Inkscape::Filters::SPOT_LIGHT;
            sp_diffuselighting->renderer->light.spot = SP_FESPOTLIGHT(sp_diffuselighting->firstChild());
        }
   }
}

void SPFeDiffuseLighting::build_renderer(Inkscape::Filters::Filter* filter) {
    g_assert(filter != nullptr);

    int primitive_n = filter->add_primitive(Inkscape::Filters::NR_FILTER_DIFFUSELIGHTING);
    Inkscape::Filters::FilterPrimitive *nr_primitive = filter->get_primitive(primitive_n);
    Inkscape::Filters::FilterDiffuseLighting *nr_diffuselighting = dynamic_cast<Inkscape::Filters::FilterDiffuseLighting*>(nr_primitive);
    g_assert(nr_diffuselighting != nullptr);

    this->renderer = nr_diffuselighting;
    this->renderer_common(nr_primitive);

    nr_diffuselighting->diffuseConstant = this->diffuseConstant;
    nr_diffuselighting->surfaceScale = this->surfaceScale;
    nr_diffuselighting->lighting_color = this->lighting_color;
    nr_diffuselighting->set_icc(this->icc);

    //We assume there is at most one child
    nr_diffuselighting->light_type = Inkscape::Filters::NO_LIGHT;

    if (SP_IS_FEDISTANTLIGHT(this->firstChild())) {
        nr_diffuselighting->light_type = Inkscape::Filters::DISTANT_LIGHT;
        nr_diffuselighting->light.distant = SP_FEDISTANTLIGHT(this->firstChild());
    }

    if (SP_IS_FEPOINTLIGHT(this->firstChild())) {
        nr_diffuselighting->light_type = Inkscape::Filters::POINT_LIGHT;
        nr_diffuselighting->light.point = SP_FEPOINTLIGHT(this->firstChild());
    }

    if (SP_IS_FESPOTLIGHT(this->firstChild())) {
        nr_diffuselighting->light_type = Inkscape::Filters::SPOT_LIGHT;
        nr_diffuselighting->light.spot = SP_FESPOTLIGHT(this->firstChild());
    }
        
    //nr_offset->set_dx(sp_offset->dx);
    //nr_offset->set_dy(sp_offset->dy);
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
