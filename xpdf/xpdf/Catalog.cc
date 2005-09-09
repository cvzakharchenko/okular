//========================================================================
//
// Catalog.cc
//
// Copyright 1996-2003 Glyph & Cog, LLC
//
//========================================================================

#include <aconf.h>

#ifdef USE_GCC_PRAGMAS
#pragma implementation
#endif

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include "gmem.h"
#include "Object.h"
#include "XRef.h"
#include "Array.h"
#include "Dict.h"
#include "Page.h"
#include "Error.h"
#include "Link.h"
#include "UGString.h"
#include "Catalog.h"

//------------------------------------------------------------------------
// Catalog
//------------------------------------------------------------------------

Catalog::Catalog(XRef *xrefA) {
  Object catDict, pagesDict;
  Object obj, obj2;
  int numPages0;
  int i;

  ok = gTrue;
  xref = xrefA;
  pages = NULL;
  pageRefs = NULL;
  numPages = pagesSize = 0;
  baseURI = NULL;
  pageMode = UseNone;

  xref->getCatalog(&catDict);
  if (!catDict.isDict()) {
    error(-1, "Catalog object is wrong type (%s)", catDict.getTypeName());
    goto err1;
  }

  // read page tree
  catDict.dictLookup("Pages", &pagesDict);
  // This should really be isDict("Pages"), but I've seen at least one
  // PDF file where the /Type entry is missing.
  if (!pagesDict.isDict()) {
    error(-1, "Top-level pages object is wrong type (%s)",
	  pagesDict.getTypeName());
    goto err2;
  }
  pagesDict.dictLookup("Count", &obj);
  // some PDF files actually use real numbers here ("/Count 9.0")
  if (!obj.isNum()) {
    error(-1, "Page count in top-level pages object is wrong type (%s)",
	  obj.getTypeName());
    goto err3;
  }
  pagesSize = numPages0 = (int)obj.getNum();
  obj.free();

  pages = (Page **)gmallocn(pagesSize, sizeof(Page *));
  pageRefs = (Ref *)gmallocn(pagesSize, sizeof(Ref));
  for (i = 0; i < pagesSize; ++i) {
    pages[i] = NULL;
    pageRefs[i].num = -1;
    pageRefs[i].gen = -1;
  }
  numPages = readPageTree(pagesDict.getDict(), NULL, 0);
  if (numPages != numPages0) {
    error(-1, "Page count in top-level pages object is incorrect");
  }
  pagesDict.free();

  // read named destination dictionary
  catDict.dictLookup("Dests", &dests);

  // read root of named destination tree
  if (catDict.dictLookup("Names", &obj)->isDict()) {
    obj.dictLookup("Dests", &obj2);
    destNameTree.init(xref, &obj2);
    obj2.free();
  }
  obj.free();

  // read base URI
  if (catDict.dictLookup("URI", &obj)->isDict()) {
    if (obj.dictLookup("Base", &obj2)->isString()) {
      baseURI = obj2.getString()->copy();
    }
    obj2.free();
  }
  obj.free();

  // read page mode
  if (catDict.dictLookup("PageMode", &obj)->isName()) {
    if (strcmp(obj.getName(), "UseNone") == 0)
      pageMode = UseNone;
    else if (strcmp(obj.getName(), "UseOutlines") == 0)
      pageMode = UseOutlines;
    else if (strcmp(obj.getName(), "UseThumbs") == 0)
      pageMode = UseThumbs;
    else if (strcmp(obj.getName(), "FullScreen") == 0)
      pageMode = FullScreen;
    else if (strcmp(obj.getName(), "UseOC") == 0)
      pageMode = UseOC;
  } else {
    pageMode = UseNone;
  }
  obj.free();

  // get the metadata stream
  catDict.dictLookup("Metadata", &metadata);

  // get the structure tree root
  catDict.dictLookup("StructTreeRoot", &structTreeRoot);

  // get the outline dictionary
  catDict.dictLookup("Outlines", &outline);

  // get the AcroForm dictionary
  catDict.dictLookup("AcroForm", &acroForm);

  catDict.free();
  return;

 err3:
  obj.free();
 err2:
  pagesDict.free();
 err1:
  catDict.free();
  dests.initNull();
  ok = gFalse;
}

Catalog::~Catalog() {
  int i;

  if (pages) {
    for (i = 0; i < pagesSize; ++i) {
      if (pages[i]) {
	delete pages[i];
      }
    }
    gfree(pages);
    gfree(pageRefs);
  }
  dests.free();
  destNameTree.free();
  if (baseURI) {
    delete baseURI;
  }
  metadata.free();
  structTreeRoot.free();
  outline.free();
  acroForm.free();
}

GString *Catalog::readMetadata() {
  GString *s;
  Dict *dict;
  Object obj;
  int c;

  if (!metadata.isStream()) {
    return NULL;
  }
  dict = metadata.streamGetDict();
  if (!dict->lookup("Subtype", &obj)->isName("XML")) {
    error(-1, "Unknown Metadata type: '%s'",
	  obj.isName() ? obj.getName() : "???");
  }
  obj.free();
  s = new GString();
  metadata.streamReset();
  while ((c = metadata.streamGetChar()) != EOF) {
    s->append(c);
  }
  metadata.streamClose();
  return s;
}

int Catalog::readPageTree(Dict *pagesDict, PageAttrs *attrs, int start) {
  Object kids;
  Object kid;
  Object kidRef;
  PageAttrs *attrs1, *attrs2;
  Page *page;
  int i, j;

  attrs1 = new PageAttrs(attrs, pagesDict);
  pagesDict->lookup("Kids", &kids);
  if (!kids.isArray()) {
    error(-1, "Kids object (page %d) is wrong type (%s)",
	  start+1, kids.getTypeName());
    goto err1;
  }
  for (i = 0; i < kids.arrayGetLength(); ++i) {
    kids.arrayGet(i, &kid);
    if (kid.isDict("Page")) {
      attrs2 = new PageAttrs(attrs1, kid.getDict());
      page = new Page(xref, start+1, kid.getDict(), attrs2);
      if (!page->isOk()) {
	++start;
	goto err3;
      }
      if (start >= pagesSize) {
	pagesSize += 32;
	pages = (Page **)greallocn(pages, pagesSize, sizeof(Page *));
	pageRefs = (Ref *)greallocn(pageRefs, pagesSize, sizeof(Ref));
	for (j = pagesSize - 32; j < pagesSize; ++j) {
	  pages[j] = NULL;
	  pageRefs[j].num = -1;
	  pageRefs[j].gen = -1;
	}
      }
      pages[start] = page;
      kids.arrayGetNF(i, &kidRef);
      if (kidRef.isRef()) {
	pageRefs[start].num = kidRef.getRefNum();
	pageRefs[start].gen = kidRef.getRefGen();
      }
      kidRef.free();
      ++start;
    // This should really be isDict("Pages"), but I've seen at least one
    // PDF file where the /Type entry is missing.
    } else if (kid.isDict()) {
      if ((start = readPageTree(kid.getDict(), attrs1, start))
	  < 0)
	goto err2;
    } else {
      error(-1, "Kid object (page %d) is wrong type (%s)",
	    start+1, kid.getTypeName());
    }
    kid.free();
  }
  delete attrs1;
  kids.free();
  return start;

 err3:
  delete page;
 err2:
  kid.free();
 err1:
  kids.free();
  delete attrs1;
  ok = gFalse;
  return -1;
}

int Catalog::findPage(int num, int gen) {
  int i;

  for (i = 0; i < numPages; ++i) {
    if (pageRefs[i].num == num && pageRefs[i].gen == gen)
      return i + 1;
  }
  return 0;
}

LinkDest *Catalog::findDest(UGString *name) {
  LinkDest *dest;
  Object obj1, obj2;
  GBool found;

  // try named destination dictionary then name tree
  found = gFalse;
  if (dests.isDict()) {
    if (!dests.dictLookup(*name, &obj1)->isNull())
      found = gTrue;
    else
      obj1.free();
  }
  if (!found) {
    if (destNameTree.lookup(name, &obj1))
      found = gTrue;
    else
      obj1.free();
  }
  if (!found)
    return NULL;

  // construct LinkDest
  dest = NULL;
  if (obj1.isArray()) {
    dest = new LinkDest(obj1.getArray());
  } else if (obj1.isDict()) {
    if (obj1.dictLookup("D", &obj2)->isArray())
      dest = new LinkDest(obj2.getArray());
    else
      error(-1, "Bad named destination value");
    obj2.free();
  } else {
    error(-1, "Bad named destination value");
  }
  obj1.free();
  if (dest && !dest->isOk()) {
    delete dest;
    dest = NULL;
  }

  return dest;
}

NameTree::NameTree(void)
{
  size = 0;
  length = 0;
  entries = NULL;
}

NameTree::Entry::Entry(Array *array, int index) {
  GString n;
  if (!array->getString(index, &n) || !array->getNF(index + 1, &value))
    error(-1, "Invalid page tree");
  name = new UGString(n);
}

NameTree::Entry::~Entry() {
  value.free();
  delete name;
}

void NameTree::addEntry(Entry *entry)
{
  if (length == size) {
    if (length == 0) {
      size = 8;
    } else {
      size *= 2;
    }
    entries = (Entry **) grealloc (entries, sizeof (Entry *) * size);
  }

  entries[length] = entry;
  ++length;
}

void NameTree::init(XRef *xrefA, Object *tree) {
  xref = xrefA;
  parse(tree);
}

void NameTree::parse(Object *tree) {
  Object names;
  Object kids, kid;
  int i;

  if (!tree->isDict())
    return;

  // leaf node
  if (tree->dictLookup("Names", &names)->isArray()) {
    for (i = 0; i < names.arrayGetLength(); i += 2) {
      NameTree::Entry *entry;

      entry = new Entry(names.getArray(), i);
      addEntry(entry);
    }
  }

  // root or intermediate node
  if (tree->dictLookup("Kids", &kids)->isArray()) {
    for (i = 0; i < kids.arrayGetLength(); ++i) {
      if (kids.arrayGet(i, &kid)->isDict())
	parse(&kid);
      kid.free();
    }
  }
  kids.free();
}

int NameTree::Entry::cmp(const void *voidKey, const void *voidEntry)
{
  UGString *key = (UGString *) voidKey;
  Entry *entry = *(NameTree::Entry **) voidEntry;

  return key->cmp(entry->name);
}

GBool NameTree::lookup(UGString *name, Object *obj)
{
  Entry *entry;

  Entry **e = (Entry **) bsearch(name, entries,
			      length, sizeof(Entry *), Entry::cmp);
  if (e) entry = *e;
  else
  {
      error(-1, "failed to look up %s\n", name->getCString());
      obj->initNull();
      return gFalse;
  }
  if (entry != NULL) {
    entry->value.fetch(xref, obj);
    return gTrue;
  } else {
    error(-1, "failed to look up %s\n", name->getCString());

    obj->initNull();

    return gFalse;
  }
}

void NameTree::free()
{
  int i;

  for (i = 0; i < length; i++)
    delete entries[i];

  gfree(entries);
}
