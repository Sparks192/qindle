/*
 * Copyright (C) 2010 Li Miao <lm3783@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "PDFModel.h"
#include <QObject>
#include <QStack>

using namespace okular;

static void pdfapp_error(pdfapp_t *app, fz_error error)
{
        qWarning("error %d",error);
}

static fz_matrix pdfapp_viewctm(pdfapp_t *app)
{
        fz_matrix ctm;
        ctm = fz_identity();
        ctm = fz_concat(ctm, fz_translate(0, -app->page->mediabox.y1));
        ctm = fz_concat(ctm, fz_scale(app->zoom, -app->zoom));
        ctm = fz_concat(ctm, fz_rotate(app->rotate + app->page->rotate));
        return ctm;
}

PDFModel::PDFModel()
{
    app=new pdfapp_t;
    memset(app, 0, sizeof(pdfapp_t));

    error = fz_newrenderer(&app->rast, pdf_devicergb, 0, 1024 * 512);
    if (error)
            pdfapp_error(app, error);
    pageRotate=0;
    app->zoom = 1.0;
}
PDFModel::~PDFModel()
{
    delete app;
}

int PDFModel::open(QString filename)
{
    fz_obj *obj;

    /*
     * Open PDF and load xref table
     */
    QByteArray file=filename.toLocal8Bit();
    app->filename = file.data();

    app->xref = pdf_newxref();
    error = pdf_loadxref(app->xref, app->filename);
    if (error)
    {
            fz_catch(error, "trying to repair");
            qWarning( "There was a problem with file \"%s\".\nIt may be corrupted or generated by faulty software.\nTrying to repair the file.", app->filename);
            error = pdf_repairxref(app->xref, app->filename);
            if (error)
                    pdfapp_error(app, error);
    }

    error = pdf_decryptxref(app->xref);
    if (error)
            pdfapp_error(app, error);

    /*
     * Handle encrypted PDF files
     */

    if (pdf_needspassword(app->xref))
    {
        char* password = this->Password.toLocal8Bit().data();
        if (!password)
            exit(1);
        int okay = pdf_authenticatepassword(app->xref, password);
        if (!okay)
            qWarning("Invalid password.");

    }

    /*
     * Load meta information
     * TODO: move this into mupdf library
     */

    obj = fz_dictgets(app->xref->trailer, "Root");
    app->xref->root = fz_resolveindirect(obj);
    if (!app->xref->root)
            pdfapp_error(app, fz_throw("syntaxerror: missing Root object"));
    fz_keepobj(app->xref->root);

    obj = fz_dictgets(app->xref->trailer, "Info");
    app->xref->info = fz_resolveindirect(obj);
    if (!app->xref->info)
            qWarning ("Could not load PDF meta information.");
    if (app->xref->info)
            fz_keepobj(app->xref->info);

    app->outline = pdf_loadoutline(app->xref);

    app->doctitle = app->filename;
    if (strrchr(app->doctitle, '\\'))
            app->doctitle = strrchr(app->doctitle, '\\') + 1;
    if (strrchr(app->doctitle, '/'))
            app->doctitle = strrchr(app->doctitle, '/') + 1;
    if (app->xref->info)
    {
            obj = fz_dictgets(app->xref->info, "Title");
            if (obj)
            {
                    app->doctitle = pdf_toutf8(obj);
            }
    }

    /*
     * Start at first page
     */

    app->pagecount = pdf_getpagecount(app->xref);

    //app->shrinkwrap = 1;
    if (app->pageno < 1)
            app->pageno = 1;
    if (app->zoom < 0.1)
            app->zoom = 0.1;
    if (app->zoom > 3.0)
            app->zoom = 3.0;
    app->rotate = 0;
    app->panx = 0;
    app->pany = 0;
    return 0;

}

void PDFModel::close()
{
    if (app->page)
            pdf_droppage(app->page);
    app->page = NULL;

    if (app->image)
            fz_droppixmap(app->image);
    app->image = NULL;

    if (app->outline)
            pdf_dropoutline(app->outline);
    app->outline = NULL;

    if (app->xref->store)
            pdf_dropstore(app->xref->store);
    app->xref->store = NULL;

    pdf_closexref(app->xref);
    app->xref = NULL;
}

int PDFModel::getCurrentPageNo()
{
    return app->pageno;
}

void PDFModel::setPageByNo(int page)
{
    //the first page should be 1, not 0.
    if(page<1)
        page=1;
    else if(page>app->pagecount)
        page=app->pagecount;
    else
        app->pageno=page;

}

int PDFModel::getTotalPage()
{
    return app->pagecount;
}

QImage PDFModel::getCurrentImage(Qt::AspectRatioMode mode)
{
    app->rotate=pageRotate;
    this->pdf_showpage(1,0);
    float winh=ViewSize.height();
    float winw=ViewSize.width();
    float pageh=(app->page->mediabox.y1 - app->page->mediabox.y0);
    float pagew=(app->page->mediabox.x1 - app->page->mediabox.x0);

    if(mode==Qt::KeepAspectRatioByExpanding) {
        if(winh / winw < pageh / pagew)
            app->zoom=winw / pagew;
        else
            app->zoom=winh / pageh;
    } else {
        if(winh / winw < pageh / pagew)
            app->zoom=winh / pageh;
        else
            app->zoom=winw / pagew;
    }

    this->pdf_showpage(0,1);
    //fitz returns a bgra image, while qimage needs argb.
    int j;
    fz_sample* i;
    fz_sample tmp;
    for(j=0;j<(app->image->h * app->image->w * app->image->n);j+=app->image->n) {
        i=app->image->samples+j;
        tmp=*i;
        *i=*(i+3);
        *(i+3)=tmp;

        tmp=*(i+1);
        *(i+1)=*(i+2);
        *(i+2)=tmp;
    }
    return QImage(app->image->samples, app->image->w, app->image->h, QImage::Format_ARGB32_Premultiplied);
}

void PDFModel::pdf_showpage(int loadpage, int drawpage)
{
    //char buf[256];
    fz_matrix ctm;
    fz_rect bbox;
    fz_obj *obj;

    if (loadpage)
    {

            if (app->page)
                    pdf_droppage(app->page);
            app->page = NULL;

            pdf_flushxref(app->xref, 0);

            obj = pdf_getpageobject(app->xref, app->pageno);
            error = pdf_loadpage(&app->page, app->xref, obj);
            if (error)
                    pdfapp_error(app, error);
            //fz_dropobj(obj);
            //sprintf(buf, "%s - %d/%d", app->doctitle, app->pageno, app->pagecount);
    }

    if (drawpage)
    {

            if (app->image)
                    fz_droppixmap(app->image);
            app->image = NULL;

            ctm = pdfapp_viewctm(app);
            bbox = fz_transformaabb(ctm, app->page->mediabox);

            error = fz_rendertree(&app->image, app->rast, app->page->tree,
                    ctm, fz_roundrect(bbox), 1);
            if (error)
                    pdfapp_error(app, error);

    }

    //pdfapp_panview(app, app->panx, app->pany);
    //we have momory leak....


}

int PDFModel::getTOC()
{
    AbstractModel::initTOC();
    //the root item
    QStandardItem* currentitem=new QStandardItem("Index");
    m_TOCModel.appendRow(currentitem);
    if(!(app->outline))
        return -1;
    QStandardItem* nameitem;
    QStandardItem* pageitem;
    QStack<pdf_outline*> stack;
    int page, i;
    pdf_outline* outline=app->outline;

    do {
        nameitem=new QStandardItem(QString::fromUtf8(outline->title));
        //Sometimes these is a title without link.
        if (outline->link) {
        page=pdf_findpageobject(app->xref, outline->link->dest);
        pageitem=new QStandardItem(QString::number(page));
    } else
        pageitem=new QStandardItem();

        i=currentitem->rowCount();
        currentitem->setChild(i,0,nameitem);
        currentitem->setChild(i,2,pageitem);

        //goto the child
        if(outline->count) {
            stack.push(outline);
            outline=outline->child;
            currentitem=nameitem;
            continue;
        }
        while(!(outline->next)) {
            if(!(stack.isEmpty())) {
                //goto parent
                outline=stack.pop();
                currentitem=currentitem->parent();
            } else
                break;
        }
        outline=outline->next;
    } while(outline);
    return 0;
}
