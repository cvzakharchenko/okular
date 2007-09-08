/***************************************************************************
 *   Copyright (C) 2006 by Pino Toscano <pino@kde.org>                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "annotationmodel.h"

#include <qlinkedlist.h>
#include <qlist.h>

#include <kicon.h>

#include "core/annotations.h"
#include "core/document.h"
#include "core/observer.h"
#include "core/page.h"

struct AnnotationItem
{
    AnnotationItem();
    AnnotationItem( AnnotationItem *parent, Okular::Annotation *ann );
    AnnotationItem( AnnotationItem *parent, int page );
    ~AnnotationItem();

    AnnotationItem *parent;
    QList< AnnotationItem* > children;

    Okular::Annotation *annotation;
    int page;
};


class AnnotationModelPrivate : public Okular::DocumentObserver
{
public:
    AnnotationModelPrivate( AnnotationModel *qq );
    virtual ~AnnotationModelPrivate();

    virtual uint observerId() const;
    virtual void notifySetup( const QVector< Okular::Page * > &pages, bool documentChanged );
    virtual void notifyPageChanged( int page, int flags );

    QModelIndex indexForItem( AnnotationItem *item ) const;
    void rebuildTree( const QVector< Okular::Page * > &pages );
    AnnotationItem* findItem( int page, int *index ) const;

    AnnotationModel *q;
    AnnotationItem *root;
    Okular::Document *document;
};


AnnotationItem::AnnotationItem()
    : parent( 0 ), annotation( 0 ), page( -1 )
{
}

AnnotationItem::AnnotationItem( AnnotationItem *_parent, Okular::Annotation *ann )
    : parent( _parent ), annotation( ann ), page( _parent->page )
{
    Q_ASSERT( !parent->annotation );
    parent->children.append( this );
}

AnnotationItem::AnnotationItem( AnnotationItem *_parent, int _page )
    : parent( _parent ), annotation( 0 ), page( _page )
{
    Q_ASSERT( !parent->parent );
    parent->children.append( this );
}

AnnotationItem::~AnnotationItem()
{
    qDeleteAll( children );
}


AnnotationModelPrivate::AnnotationModelPrivate( AnnotationModel *qq )
    : q( qq ), root( new AnnotationItem )
{
}

AnnotationModelPrivate::~AnnotationModelPrivate()
{
    delete root;
}

uint AnnotationModelPrivate::observerId() const
{
    return ANNOTATIONMODEL_ID;
}

void AnnotationModelPrivate::notifySetup( const QVector< Okular::Page * > &pages, bool documentChanged )
{
    if ( !documentChanged )
        return;

    qDeleteAll( root->children );
    root->children.clear();
    q->reset();

    rebuildTree( pages );
}

void AnnotationModelPrivate::notifyPageChanged( int page, int flags )
{
    // we are strictly interested in annotations
    if ( !(flags & Okular::DocumentObserver::Annotations ) )
        return;

    QLinkedList< Okular::Annotation* > annots = document->page( page )->annotations();
    int annItemIndex = -1;
    AnnotationItem *annItem = findItem( page, &annItemIndex );
    // case 1: the page has no more annotations
    //         => remove the branch, if any
    if ( annots.isEmpty() )
    {
        if ( annItem )
        {
            q->beginRemoveRows( indexForItem( root ), annItemIndex, annItemIndex );
            delete root->children.at( annItemIndex );
            root->children.removeAt( annItemIndex );
            q->endRemoveRows();
        }
        return;
    }
    // case 2: no existing branch
    //         => add a new branch, and add the annotations for the page
    if ( !annItem )
    {
        int i = 0;
        while ( i < root->children.count() && root->children.at( i )->page < page ) ++i;

        AnnotationItem *annItem = new AnnotationItem();
        annItem->page = page;
        annItem->parent = root;
        q->beginInsertRows( indexForItem( root ), i, i );
        annItem->parent->children.insert( i, annItem );
        q->endInsertRows();
        QLinkedList< Okular::Annotation* >::ConstIterator it = annots.begin(), itEnd = annots.end();
        int newid = 0;
        for ( ; it != itEnd; ++it, ++newid )
        {
            q->beginInsertRows( indexForItem( annItem ), newid, newid );
            new AnnotationItem( annItem, *it );
            q->endInsertRows();
        }
        return;
    }
    // case 3: existing branch, less annotations than items
    //         => lookup and remove the annotations
    if ( annItem->children.count() > annots.count() )
    {
        for ( int i = annItem->children.count(); i > 0; --i )
        {
            Okular::Annotation *ref = annItem->children.at( i - 1 )->annotation;
            bool found = false;
            QLinkedList< Okular::Annotation* >::ConstIterator it = annots.begin(), itEnd = annots.end();
            for ( ; !found && it != itEnd; ++it )
            {
                if ( ( *it ) == ref )
                    found = true;
            }
            if ( !found )
            {
                q->beginRemoveRows( indexForItem( annItem ), i - 1, i - 1 );
                annItem->children.removeAt( i - 1 );
                q->endRemoveRows();
            }
        }
        return;
    }
    // case 4: existing branch, less items than annotations
    //         => lookup and add annotations if not in the branch
    if ( annots.count() > annItem->children.count() )
    {
        QLinkedList< Okular::Annotation* >::ConstIterator it = annots.begin(), itEnd = annots.end();
        for ( ; it != itEnd; ++it )
        {
            Okular::Annotation *ref = *it;
            bool found = false;
            int count = annItem->children.count();
            for ( int i = 0; !found && i < count; ++i )
            {
                if ( ref == annItem->children.at( i )->annotation )
                    found = true;
            }
            if ( !found )
            {
                q->beginInsertRows( indexForItem( annItem ), count, count );
                new AnnotationItem( annItem, ref );
                q->endInsertRows();
            }
        }
        return;
    }
    // case 5: the data of some annotation changed
    // TODO: what do we do in this case?
}

QModelIndex AnnotationModelPrivate::indexForItem( AnnotationItem *item ) const
{
    if ( item->parent )
    {
        int id = item->parent->children.indexOf( item );
        if ( id >= 0 && id < item->parent->children.count() )
           return q->createIndex( id, 0, item );
    }
    return QModelIndex();
}

void AnnotationModelPrivate::rebuildTree( const QVector< Okular::Page * > &pages )
{
    emit q->layoutAboutToBeChanged();
    for ( int i = 0; i < pages.count(); ++i )
    {
        QLinkedList< Okular::Annotation* > annots = pages.at( i )->annotations();
        if ( annots.isEmpty() )
            continue;

        AnnotationItem *annItem = new AnnotationItem( root, i );
        QLinkedList< Okular::Annotation* >::ConstIterator it = annots.begin(), itEnd = annots.end();
        for ( ; it != itEnd; ++it )
        {
            new AnnotationItem( annItem, *it );
        }
    }
    emit q->layoutChanged();
}

AnnotationItem* AnnotationModelPrivate::findItem( int page, int *index ) const
{
    for ( int i = 0; i < root->children.count(); ++i )
    {
        AnnotationItem *tmp = root->children.at( i );
        if ( tmp->page == page )
        {
            if ( index )
                *index = i;
            return tmp;
        }
    }
    if ( index )
        *index = -1;
    return 0;
}


AnnotationModel::AnnotationModel( Okular::Document *document, QObject *parent )
    : QAbstractItemModel( parent ), d( new AnnotationModelPrivate( this ) )
{
    d->document = document;

    d->document->addObserver( d );
}

AnnotationModel::~AnnotationModel()
{
    delete d;
}

int AnnotationModel::columnCount( const QModelIndex &parent ) const
{
    Q_UNUSED( parent )
    return 1;
}

QVariant AnnotationModel::data( const QModelIndex &index, int role ) const
{
    if ( !index.isValid() )
        return QVariant();

    AnnotationItem *item = static_cast< AnnotationItem* >( index.internalPointer() );
    if ( !item->annotation )
    {
        if ( role == Qt::DisplayRole || role == PageRole )
           return item->page;
        return QVariant();
    }
    switch ( role )
    {
        case Qt::DisplayRole:
            return item->annotation->author();
            break;
        case Qt::DecorationRole:
            return KIcon( "okular" );
            break;
        case AuthorRole:
            return item->annotation->author();
            break;
        case PageRole:
            return item->page;
            break;
    }
    return QVariant();
}

bool AnnotationModel::hasChildren( const QModelIndex &parent ) const
{
    if ( !parent.isValid() )
        return true;

    AnnotationItem *item = static_cast< AnnotationItem* >( parent.internalPointer() );
    return !item->children.isEmpty();
}

QVariant AnnotationModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( orientation != Qt::Horizontal )
        return QVariant();

    if ( section == 0 && role == Qt::DisplayRole )
        return "Annotations";

    return QVariant();
}

QModelIndex AnnotationModel::index( int row, int column, const QModelIndex &parent ) const
{
    if ( row < 0 || column != 0 )
        return QModelIndex();

    AnnotationItem *item = parent.isValid() ? static_cast< AnnotationItem* >( parent.internalPointer() ) : d->root;
    if ( row < item->children.count() )
        return createIndex( row, column, item->children.at( row ) );

    return QModelIndex();
}

QModelIndex AnnotationModel::parent( const QModelIndex &index ) const
{
    if ( !index.isValid() )
        return QModelIndex();

    AnnotationItem *item = static_cast< AnnotationItem* >( index.internalPointer() );
    return d->indexForItem( item->parent );
}

int AnnotationModel::rowCount( const QModelIndex &parent ) const
{
    AnnotationItem *item = parent.isValid() ? static_cast< AnnotationItem* >( parent.internalPointer() ) : d->root;
    return item->children.count();
}

bool AnnotationModel::isAnnotation( const QModelIndex &index ) const
{
    return annotationForIndex( index );
}

Okular::Annotation* AnnotationModel::annotationForIndex( const QModelIndex &index ) const
{
    if ( !index.isValid() )
        return 0;

    AnnotationItem *item = static_cast< AnnotationItem* >( index.internalPointer() );
    return item->annotation;
}

#include "annotationmodel.moc"
