/*
 * Section utility functions
 *
 *  Copyright (C) 2001  Peter Johnson
 *
 *  This file is part of YASM.
 *
 *  YASM is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  YASM is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "util.h"
/*@unused@*/ RCSID("$IdPath$");

#include "errwarn.h"
#include "intnum.h"
#include "expr.h"

#include "bytecode.h"
#include "section.h"
#include "objfmt.h"


struct section {
    /*@reldef@*/ STAILQ_ENTRY(section) link;

    enum { SECTION_GENERAL, SECTION_ABSOLUTE } type;

    union {
	/* SECTION_GENERAL data */
	struct general {
	    /*@owned@*/ char *name;	/* strdup()'ed name (given by user) */

	    /* object-format-specific data */
	    /*@null@*/ /*@dependent@*/ objfmt *of;
	    /*@null@*/ /*@owned@*/ void *of_data;
	} general;
    } data;

    /*@owned@*/ expr *start;	/* Starting address of section contents */

    unsigned long opt_flags;	/* storage for optimizer flags */

    int res_only;		/* allow only resb family of bytecodes? */

    bytecodehead bc;		/* the bytecodes for the section's contents */
};

/*@-compdestroy@*/
section *
sections_initialize(sectionhead *headp, objfmt *of)
{
    section *s;
    valparamhead vps;
    valparam *vp;

    /* Initialize linked list */
    STAILQ_INIT(headp);

    /* Add an initial "default" section */
    vp_new(vp, xstrdup(of->default_section_name), NULL);
    vps_initialize(&vps);
    vps_append(&vps, vp);
    s = of->sections_switch(headp, &vps, NULL, 0);
    vps_delete(&vps);

    return s;
}
/*@=compdestroy@*/

/*@-onlytrans@*/
section *
sections_switch_general(sectionhead *headp, const char *name,
			unsigned long start, int res_only, int *isnew,
			unsigned long lindex)
{
    section *s;

    /* Search through current sections to see if we already have one with
     * that name.
     */
    STAILQ_FOREACH(s, headp, link) {
	if (s->type == SECTION_GENERAL &&
	    strcmp(s->data.general.name, name) == 0) {
	    *isnew = 0;
	    return s;
	}
    }

    /* No: we have to allocate and create a new one. */

    /* Okay, the name is valid; now allocate and initialize */
    s = xcalloc(1, sizeof(section));
    STAILQ_INSERT_TAIL(headp, s, link);

    s->type = SECTION_GENERAL;
    s->data.general.name = xstrdup(name);
    s->data.general.of = NULL;
    s->data.general.of_data = NULL;
    s->start = expr_new_ident(ExprInt(intnum_new_uint(start)), lindex);
    bcs_initialize(&s->bc);

    s->opt_flags = 0;
    s->res_only = res_only;

    *isnew = 1;
    return s;
}
/*@=onlytrans@*/

/*@-onlytrans@*/
section *
sections_switch_absolute(sectionhead *headp, expr *start)
{
    section *s;

    s = xcalloc(1, sizeof(section));
    STAILQ_INSERT_TAIL(headp, s, link);

    s->type = SECTION_ABSOLUTE;
    s->start = start;
    bcs_initialize(&s->bc);

    s->opt_flags = 0;
    s->res_only = 1;

    return s;
}
/*@=onlytrans@*/

int
section_is_absolute(section *sect)
{
    return (sect->type == SECTION_ABSOLUTE);
}

unsigned long
section_get_opt_flags(const section *sect)
{
    return sect->opt_flags;
}

void
section_set_opt_flags(section *sect, unsigned long opt_flags)
{
    sect->opt_flags = opt_flags;
}

void
section_set_of_data(section *sect, objfmt *of, void *of_data)
{
    /* Check to see if section type supports of_data */
    if (sect->type != SECTION_GENERAL) {
	if (of->section_data_delete)
	    of->section_data_delete(of_data);
	else
	    InternalError(_("don't know how to delete objfmt-specific section data"));
	return;
    }

    /* Delete current of_data if present */
    if (sect->data.general.of_data && sect->data.general.of) {
	objfmt *of2 = sect->data.general.of;
	if (of2->section_data_delete)
	    of2->section_data_delete(sect->data.general.of_data);
	else
	    InternalError(_("don't know how to delete objfmt-specific section data"));
    }

    /* Assign new of_data */
    sect->data.general.of = of;
    sect->data.general.of_data = of_data;
}

void *
section_get_of_data(section *sect)
{
    if (sect->type == SECTION_GENERAL)
	return sect->data.general.of_data;
    else
	return NULL;
}

void
sections_delete(sectionhead *headp)
{
    section *cur, *next;

    cur = STAILQ_FIRST(headp);
    while (cur) {
	next = STAILQ_NEXT(cur, link);
	section_delete(cur);
	cur = next;
    }
    STAILQ_INIT(headp);
}

void
sections_print(FILE *f, int indent_level, const sectionhead *headp)
{
    section *cur;
    
    STAILQ_FOREACH(cur, headp, link) {
	fprintf(f, "%*sSection:\n", indent_level, "");
	section_print(f, indent_level+1, cur, 1);
    }
}

int
sections_traverse(sectionhead *headp, /*@null@*/ void *d,
		  int (*func) (section *sect, /*@null@*/ void *d))
{
    section *cur;
    
    STAILQ_FOREACH(cur, headp, link) {
	int retval = func(cur, d);
	if (retval != 0)
	    return retval;
    }
    return 0;
}

/*@-onlytrans@*/
section *
sections_find_general(sectionhead *headp, const char *name)
{
    section *cur;

    STAILQ_FOREACH(cur, headp, link) {
	if (cur->type == SECTION_GENERAL &&
	    strcmp(cur->data.general.name, name) == 0)
	    return cur;
    }
    return NULL;
}
/*@=onlytrans@*/

bytecodehead *
section_get_bytecodes(section *sect)
{
    return &sect->bc;
}

const char *
section_get_name(const section *sect)
{
    if (sect->type == SECTION_GENERAL)
	return sect->data.general.name;
    return NULL;
}

void
section_set_start(section *sect, unsigned long start, unsigned long lindex)
{
    expr_delete(sect->start);
    sect->start = expr_new_ident(ExprInt(intnum_new_uint(start)), lindex);
}

const expr *
section_get_start(const section *sect)
{
    return sect->start;
}

void
section_delete(section *sect)
{
    if (!sect)
	return;

    if (sect->type == SECTION_GENERAL) {
	xfree(sect->data.general.name);
	if (sect->data.general.of_data && sect->data.general.of) {
	    objfmt *of = sect->data.general.of;
	    if (of->section_data_delete)
		of->section_data_delete(sect->data.general.of_data);
	    else
		InternalError(_("don't know how to delete objfmt-specific section data"));
	}
    }
    expr_delete(sect->start);
    bcs_delete(&sect->bc);
    xfree(sect);
}

void
section_print(FILE *f, int indent_level, const section *sect, int print_bcs)
{
    if (!sect) {
	fprintf(f, "%*s(none)\n", indent_level, "");
	return;
    }

    fprintf(f, "%*stype=", indent_level, "");
    switch (sect->type) {
	case SECTION_GENERAL:
	    fprintf(f, "general\n%*sname=%s\n%*sobjfmt data:\n", indent_level,
		    "", sect->data.general.name, indent_level, "");
	    indent_level++;
	    if (sect->data.general.of_data && sect->data.general.of) {
		objfmt *of = sect->data.general.of;
		if (of->section_data_print)
		    of->section_data_print(f, indent_level,
					   sect->data.general.of_data);
		else
		    fprintf(f, "%*sUNKNOWN\n", indent_level, "");
	    } else
		fprintf(f, "%*s(none)\n", indent_level, "");
	    indent_level--;
	    break;
	case SECTION_ABSOLUTE:
	    fprintf(f, "absolute\n");
	    break;
    }

    fprintf(f, "%*sstart=", indent_level, "");
    expr_print(f, sect->start);
    fprintf(f, "\n");

    if (print_bcs) {
	fprintf(f, "%*sBytecodes:\n", indent_level, "");
	bcs_print(f, indent_level+1, &sect->bc);
    }
}
