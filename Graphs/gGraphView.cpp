/*
 gGraphView Implementation
 Copyright (c)2011 Mark Watkins <jedimark@users.sourceforge.net>
 License: GPL
*/

#include <cmath>
#include <QFontMetrics>
#include "gGraphView.h"
#include "SleepLib/profiles.h"
#include <QTimer>
#include <QLabel>
#include <QDir>
#include "mainwindow.h"

#include "Graphs/gYAxis.h"
#include "Graphs/gFlagsLine.h"
#include "gLineChart.h"


extern MainWindow *mainwin;

#ifdef Q_WS_MAC
#define USE_RENDERTEXT
#include "OpenGL/glu.h"
#else
#include "GL/glu.h"
#endif

#ifdef DEBUG_EFFICIENCY
// Only works in 4.8
#include <QElapsedTimer>
#endif


bool _graph_init=false;

QFont * defaultfont=NULL;
QFont * mediumfont=NULL;
QFont * bigfont=NULL;

bool evil_intel_graphics_chip=false;

extern QLabel * qstatus2;
const int mouse_movement_threshold=6;

QHash<QString, QImage *> images;

// Must be called from a thread inside the application.
void InitGraphs()
{
    if (!_graph_init) {

        if (!PREF.contains("Fonts_Graph_Name")) {
            PREF["Fonts_Graph_Name"]="Sans Serif";
            PREF["Fonts_Graph_Size"]=10;
            PREF["Fonts_Graph_Bold"]=false;
            PREF["Fonts_Graph_Italic"]=false;
        }
        if (!PREF.contains("Fonts_Title_Name")) {
            PREF["Fonts_Title_Name"]="Serif";
            PREF["Fonts_Title_Size"]=11;
            PREF["Fonts_Title_Bold"]=true;
            PREF["Fonts_Title_Italic"]=false;
        }
        if (!PREF.contains("Fonts_Big_Name")) {
            PREF["Fonts_Big_Name"]="Serif";
            PREF["Fonts_Big_Size"]=35;
            PREF["Fonts_Big_Bold"]=false;
            PREF["Fonts_Big_Italic"]=false;
        }

        defaultfont=new QFont(PREF["Fonts_Graph_Name"].toString(),
                              PREF["Fonts_Graph_Size"].toInt(),
                              PREF["Fonts_Graph_Bold"].toBool() ? QFont::Bold : QFont::Normal,
                              PREF["Fonts_Graph_Italic"].toBool()
                        );
        mediumfont=new QFont(PREF["Fonts_Title_Name"].toString(),
                             PREF["Fonts_Title_Size"].toInt(),
                             PREF["Fonts_Title_Bold"].toBool() ? QFont::Bold : QFont::Normal,
                             PREF["Fonts_Title_Italic"].toBool()
                        );
        bigfont=new QFont(PREF["Fonts_Big_Name"].toString(),
                          PREF["Fonts_Big_Size"].toInt(),
                          PREF["Fonts_Big_Bold"].toBool() ? QFont::Bold : QFont::Normal,
                          PREF["Fonts_Big_Italic"].toBool()
                     );

        defaultfont->setStyleHint(QFont::AnyStyle,QFont::OpenGLCompatible);
        mediumfont->setStyleHint(QFont::AnyStyle,QFont::OpenGLCompatible);
        bigfont->setStyleHint(QFont::AnyStyle,QFont::OpenGLCompatible);

        //images["mask"]=new QImage(":/icons/mask.png");
        images["oximeter"]=new QImage(":/icons/cubeoximeter.png");
        images["smiley"]=new QImage(":/icons/smileyface.png");
        //images["sad"]=new QImage(":/icons/sadface.png");

        images["sheep"]=new QImage(":/icons/sheep.png");
        images["brick"]=new QImage(":/icons/brick.png");
        images["nographs"]=new QImage(":/icons/nographs.png");
        images["nodata"]=new QImage(":/icons/nodata.png");

        _graph_init=true;
    }
}
void DoneGraphs()
{
    if (_graph_init) {
        delete defaultfont;
        delete bigfont;
        delete mediumfont;
        for (QHash<QString,QImage *>::iterator i=images.begin();i!=images.end();i++) {
            delete i.value();
        }
        _graph_init=false;
    }
}

void GetTextExtent(QString text, int & width, int & height, QFont *font)
{
#ifdef ENABLE_THREADED_DRAWING
    static QMutex mut;
    mut.lock();
#endif
    QFontMetrics fm(*font);
//#ifdef Q_WS_WIN32
    QRect r=fm.tightBoundingRect(text);
    width=r.width();
    height=r.height();
//#else
//    width=fm.width(text);
//    height=fm.xHeight()+2; // doesn't work properly on windows..
//#endif
#ifdef ENABLE_THREADED_DRAWING
    mut.unlock();
#endif
}

int GetXHeight(QFont *font)
{
    QFontMetrics fm(*font);
    return fm.xHeight();
}

inline quint32 swaporder(quint32 color)
{
  return ((color & 0xFF00FF00) |
         ((color & 0xFF0000) >> 16)|
         ((color & 0xFF) << 16));
}

gVertexBuffer::gVertexBuffer(int max,int type)
:m_max(max), m_type(type), m_cnt(0), m_size(1), m_scissor(false), m_stippled(false), m_stipple(0xffff)
{
    buffer=(gVertex *)calloc(max,sizeof(gVertex));
    m_blendfunc1=GL_SRC_ALPHA;
    m_blendfunc2=GL_ONE_MINUS_SRC_ALPHA;
    m_antialias=m_forceantialias=false;
}
gVertexBuffer::~gVertexBuffer()
{
    free(buffer);
}
void gVertexBuffer::setColor(QColor col)
{
    m_color=swaporder(col.rgba());
}

void gVertexBuffer::draw()
{
    bool antialias=m_forceantialias || (PROFILE.appearance->antiAliasing() && m_antialias);
    if (m_stippled) antialias=false;
    float size=m_size;

    if (antialias) {
        glEnable(GL_BLEND);
        glBlendFunc(m_blendfunc1,  m_blendfunc2);
        if (m_type==GL_LINES || m_type==GL_LINE_LOOP) {
            glEnable(GL_LINE_SMOOTH);
            glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
            size+=0.5;
        } else if (m_type==GL_POLYGON) {
            glEnable(GL_POLYGON_SMOOTH);
            glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
        }
    }
    if (m_type==GL_LINES || m_type==GL_LINE_LOOP) {
        if (m_stippled) {
            glLineStipple(1, m_stipple);
            //size=1;
            glEnable(GL_LINE_STIPPLE);
        } else {
            //glLineStipple(1, 0xFFFF);
        }
        glLineWidth(size);

    } else if (m_type==GL_POINTS) {
        glPointSize(size);
    } else if (m_type==GL_POLYGON) {
        glPolygonMode(GL_BACK,GL_FILL);
    }
    if (m_scissor) {
        glScissor(s_x,s_y,s_width,s_height);
        glEnable(GL_SCISSOR_TEST);
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_SHORT, 8, (GLvoid *)buffer);
    glColorPointer(4, GL_UNSIGNED_BYTE, 8, ((char *)buffer)+4);

    glDrawArrays(m_type, 0, m_cnt);

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    m_cnt=0;
    if (m_scissor) {
        glDisable(GL_SCISSOR_TEST);
        m_scissor=false;
    }
    if (m_type==GL_POLYGON) {
        glPolygonMode(GL_BACK,GL_FILL);
    }
    if (m_type==GL_LINES || m_type==GL_LINE_LOOP) {
        if (m_stippled) {
            glDisable(GL_LINE_STIPPLE);
            glLineStipple(1, 0xFFFF);
        }
    }

    if (antialias) {
        if (m_type==GL_LINES || m_type==GL_LINE_LOOP) {
            glDisable(GL_LINE_SMOOTH);
        } else if (m_type==GL_POLYGON) {
            glDisable(GL_POLYGON_SMOOTH);
        }
        glDisable(GL_BLEND);
    }
}
void gVertexBuffer::add(GLshort x1, GLshort y1, RGBA color)
{
    if (m_cnt<m_max) {
        gVertex & v=buffer[m_cnt];

        v.color=swaporder(color);
        v.x=x1;
        v.y=y1;

        m_cnt++;
    }
}
void gVertexBuffer::add(GLshort x1, GLshort y1, GLshort x2, GLshort y2, RGBA color)
{
    if (m_cnt < (m_max-1)) {
        gVertex * v=&buffer[m_cnt];

        v->x=x1;
        v->y=y1;
        v->color=swaporder(color);

        v++;
        v->x=x2;
        v->y=y2;
        v->color=swaporder(color);

        m_cnt+=2;
    }
}
void gVertexBuffer::add(GLshort x1, GLshort y1, GLshort x2, GLshort y2, GLshort x3, GLshort y3, GLshort x4, GLshort y4, RGBA color)
{
    if (m_cnt < (m_max-3)) {
        gVertex *v=&buffer[m_cnt];

        v->color=swaporder(color);
        v->x=x1;
        v->y=y1;
        v++;

        v->color=swaporder(color);
        v->x=x2;
        v->y=y2;

        v++;
        v->color=swaporder(color);
        v->x=x3;
        v->y=y3;

        v++;
        v->color=swaporder(color);
        v->x=x4;
        v->y=y4;

        m_cnt+=4;
    }
}

void gVertexBuffer::add(GLshort x1, GLshort y1, GLshort x2, GLshort y2, GLshort x3, GLshort y3, GLshort x4, GLshort y4, RGBA color1, RGBA color2)
{
    if (m_cnt < (m_max-3)) {
        gVertex *v=&buffer[m_cnt];

        v->color=swaporder(color1);
        v->x=x1;
        v->y=y1;
        v++;

        v->color=swaporder(color1);
        v->x=x2;
        v->y=y2;

        v++;
        v->color=swaporder(color2);
        v->x=x3;
        v->y=y3;

        v++;
        v->color=swaporder(color2);
        v->x=x4;
        v->y=y4;

        m_cnt+=4;
    }
}
void gVertexBuffer::unsafe_add(GLshort x1, GLshort y1)
{
    gVertex & v=buffer[m_cnt++];

    v.color=m_color;
    v.x=x1;
    v.y=y1;
}
void gVertexBuffer::add(GLshort x1, GLshort y1)
{
    if (m_cnt<m_max) {
        gVertex & v=buffer[m_cnt++];

        v.color=m_color;
        v.x=x1;
        v.y=y1;
    }
}
void gVertexBuffer::unsafe_add(GLshort x1, GLshort y1, GLshort x2, GLshort y2)
{
    gVertex * v=&buffer[m_cnt];

    v->x=x1;
    v->y=y1;
    v->color=m_color;

    v++;
    v->x=x2;
    v->y=y2;
    v->color=m_color;

    m_cnt+=2;
}

void gVertexBuffer::add(GLshort x1, GLshort y1, GLshort x2, GLshort y2)
{
    if (m_cnt < (m_max-1)) {
        gVertex * v=&buffer[m_cnt];

        v->x=x1;
        v->y=y1;
        v->color=m_color;

        v++;
        v->x=x2;
        v->y=y2;
        v->color=m_color;

        m_cnt+=2;
    }
}
void gVertexBuffer::add(GLshort x1, GLshort y1, GLshort x2, GLshort y2, GLshort x3, GLshort y3, GLshort x4, GLshort y4)
{
    if (m_cnt < (m_max-3)) {
        gVertex *v=&buffer[m_cnt];

        v->color=m_color;
        v->x=x1;
        v->y=y1;
        v++;

        v->color=m_color;
        v->x=x2;
        v->y=y2;

        v++;
        v->color=m_color;
        v->x=x3;
        v->y=y3;

        v++;
        v->color=m_color;
        v->x=x4;
        v->y=y4;

        m_cnt+=4;
    }
}
void gVertexBuffer::unsafe_add(GLshort x1, GLshort y1, GLshort x2, GLshort y2, GLshort x3, GLshort y3, GLshort x4, GLshort y4)
{
    gVertex *v=&buffer[m_cnt];

    v->color=m_color;
    v->x=x1;
    v->y=y1;
    v++;

    v->color=m_color;
    v->x=x2;
    v->y=y2;

    v++;
    v->color=m_color;
    v->x=x3;
    v->y=y3;

    v++;
    v->color=m_color;
    v->x=x4;
    v->y=y4;

    m_cnt+=4;
}


GLBuffer::GLBuffer(int max,int type, bool stippled)
    :m_max(max), m_type(type), m_stippled(stippled)
{
    m_scissor=false;
    m_antialias=true;
    m_forceantialias=false;

    m_cnt=0;
    m_colcnt=0;
    m_size=1;
    m_blendfunc1=GL_SRC_ALPHA;
    m_blendfunc2=GL_ONE_MINUS_SRC_ALPHA;
}
GLBuffer::~GLBuffer()
{
}

/////////////////////////////////////////////////////////////////////
// GLFloatBuffer

GLFloatBuffer::GLFloatBuffer(int max,int type,bool stippled)
    :GLBuffer(max,type,stippled)
{
    buffer=new GLfloat [max+8];
    colors=new GLubyte[max*4+(8*4)];
}
GLFloatBuffer::~GLFloatBuffer()
{
    if (colors) delete [] colors;
    if (buffer) delete [] buffer;
}

void GLFloatBuffer::add(GLfloat x, GLfloat y,QColor & color)
{
    if (m_cnt<m_max+2) {
#ifdef ENABLE_THREADED_DRAWING
        mutex.lock();
#endif
        buffer[m_cnt++]=x;
        buffer[m_cnt++]=y;
        colors[m_colcnt++]=color.red();
        colors[m_colcnt++]=color.green();
        colors[m_colcnt++]=color.blue();
        colors[m_colcnt++]=color.alpha();
#ifdef ENABLE_THREADED_DRAWING
        mutex.unlock();
#endif
    } else {
        qDebug() << "GLBuffer overflow";
    }
}
void GLFloatBuffer::add(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2,QColor & color)
{
    if (m_cnt<m_max+4) {
        qDebug() << "GLFloatBuffer overflow";
        return;
    }
#ifdef ENABLE_THREADED_DRAWING
    mutex.lock();
#endif
    buffer[m_cnt++]=x1;
    buffer[m_cnt++]=y1;
    buffer[m_cnt++]=x2;
    buffer[m_cnt++]=y2;
    colors[m_colcnt++]=color.red();
    colors[m_colcnt++]=color.green();
    colors[m_colcnt++]=color.blue();
    colors[m_colcnt++]=color.alpha();
    colors[m_colcnt++]=color.red();
    colors[m_colcnt++]=color.green();
    colors[m_colcnt++]=color.blue();
    colors[m_colcnt++]=color.alpha();

#ifdef ENABLE_THREADED_DRAWING
    mutex.unlock();
#endif
}
void GLFloatBuffer::add(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2,GLfloat x3, GLfloat y3, GLfloat x4, GLfloat y4,QColor & color) // add with vertex colors
{
    if (m_cnt>=m_max+8) {
        qDebug() << "GLFloatBuffer overflow";
        return;
    }
#ifdef ENABLE_THREADED_DRAWING
    mutex.lock();
#endif
    buffer[m_cnt++]=x1;
    buffer[m_cnt++]=y1;
    buffer[m_cnt++]=x2;
    buffer[m_cnt++]=y2;
    buffer[m_cnt++]=x3;
    buffer[m_cnt++]=y3;
    buffer[m_cnt++]=x4;
    buffer[m_cnt++]=y4;
    colors[m_colcnt++]=color.red();
    colors[m_colcnt++]=color.green();
    colors[m_colcnt++]=color.blue();
    colors[m_colcnt++]=color.alpha();
    colors[m_colcnt++]=color.red();
    colors[m_colcnt++]=color.green();
    colors[m_colcnt++]=color.blue();
    colors[m_colcnt++]=color.alpha();
    colors[m_colcnt++]=color.red();
    colors[m_colcnt++]=color.green();
    colors[m_colcnt++]=color.blue();
    colors[m_colcnt++]=color.alpha();
    colors[m_colcnt++]=color.red();
    colors[m_colcnt++]=color.green();
    colors[m_colcnt++]=color.blue();
    colors[m_colcnt++]=color.alpha();
#ifdef ENABLE_THREADED_DRAWING
    mutex.unlock();
#endif
}
void GLFloatBuffer::quadGrLR(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2,GLfloat x3, GLfloat y3, GLfloat x4, GLfloat y4,QColor & color, QColor & color2) // add with vertex colors
{
    if (m_cnt>=m_max+8) {
        qDebug() << "GLFloatBuffer overflow";
        return;
    }
#ifdef ENABLE_THREADED_DRAWING
    mutex.lock();
#endif
    buffer[m_cnt++]=x1;
    buffer[m_cnt++]=y1;
    buffer[m_cnt++]=x2;
    buffer[m_cnt++]=y2;
    buffer[m_cnt++]=x3;
    buffer[m_cnt++]=y3;
    buffer[m_cnt++]=x4;
    buffer[m_cnt++]=y4;
    colors[m_colcnt++]=color.red();
    colors[m_colcnt++]=color.green();
    colors[m_colcnt++]=color.blue();
    colors[m_colcnt++]=color.alpha();
    colors[m_colcnt++]=color.red();
    colors[m_colcnt++]=color.green();
    colors[m_colcnt++]=color.blue();
    colors[m_colcnt++]=color.alpha();

    colors[m_colcnt++]=color2.red();
    colors[m_colcnt++]=color2.green();
    colors[m_colcnt++]=color2.blue();
    colors[m_colcnt++]=color2.alpha();
    colors[m_colcnt++]=color2.red();
    colors[m_colcnt++]=color2.green();
    colors[m_colcnt++]=color2.blue();
    colors[m_colcnt++]=color2.alpha();
#ifdef ENABLE_THREADED_DRAWING
    mutex.unlock();
#endif
}
void GLFloatBuffer::quadGrTB(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2,GLfloat x3, GLfloat y3, GLfloat x4, GLfloat y4,QColor & color, QColor & color2)
{
    if (m_cnt>=m_max+8) {
        qDebug() << "GLFloatBuffer overflow";
        return;
    }
#ifdef ENABLE_THREADED_DRAWING
    mutex.lock();
#endif
    buffer[m_cnt++]=x1;
    buffer[m_cnt++]=y1;
    buffer[m_cnt++]=x3;
    buffer[m_cnt++]=y3;
    buffer[m_cnt++]=x2;
    buffer[m_cnt++]=y2;
    buffer[m_cnt++]=x4;
    buffer[m_cnt++]=y4;
    colors[m_colcnt++]=color.red();
    colors[m_colcnt++]=color.green();
    colors[m_colcnt++]=color.blue();
    colors[m_colcnt++]=color.alpha();
    colors[m_colcnt++]=color.red();
    colors[m_colcnt++]=color.green();
    colors[m_colcnt++]=color.blue();
    colors[m_colcnt++]=color.alpha();

    colors[m_colcnt++]=color2.red();
    colors[m_colcnt++]=color2.green();
    colors[m_colcnt++]=color2.blue();
    colors[m_colcnt++]=color2.alpha();
    colors[m_colcnt++]=color2.red();
    colors[m_colcnt++]=color2.green();
    colors[m_colcnt++]=color2.blue();
    colors[m_colcnt++]=color2.alpha();
#ifdef ENABLE_THREADED_DRAWING
    mutex.unlock();
#endif
}

void GLFloatBuffer::draw()
{
    if (m_cnt<=0) return;

    bool antialias=m_forceantialias || (PROFILE.appearance->antiAliasing() && m_antialias);
    float size=m_size;
    if (antialias) {
        glEnable(GL_BLEND);
        glBlendFunc(m_blendfunc1,  m_blendfunc2);
        if (m_type==GL_LINES || m_type==GL_LINE_LOOP) {
            glEnable(GL_LINE_SMOOTH);
            glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
            size+=0.5;
        } else if (m_type==GL_POLYGON) {
            glEnable(GL_POLYGON_SMOOTH);
            glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
        }
    }
    if (m_type==GL_LINES || m_type==GL_LINE_LOOP) {
        glLineWidth(size);
        if (m_stippled) {
            glLineStipple(1, 0xAAAA);
            glEnable(GL_LINE_STIPPLE);
        }
    } else if (m_type==GL_POINTS) {
        glPointSize(size);
    } else if (m_type==GL_POLYGON) {
        glPolygonMode(GL_BACK,GL_FILL);
    }
    if (m_scissor) {
        glScissor(s1,s2,s3,s4);
        glEnable(GL_SCISSOR_TEST);
    }

    glVertexPointer(2, GL_FLOAT, 0, buffer);

    glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);

    //glColor4ub(200,128,95,200);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glDrawArrays(m_type, 0, m_cnt >> 1);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    //qDebug() << "I Drawed" << m_cnt << "vertices";
    m_cnt=0;
    m_colcnt=0;
    if (m_scissor) {
        glDisable(GL_SCISSOR_TEST);
        m_scissor=false;
    }
    if (m_type==GL_POLYGON) {
        glPolygonMode(GL_BACK,GL_FILL);
    }
    if (antialias) {
        if (m_type==GL_LINES || m_type==GL_LINE_LOOP) {
            if (m_stippled) glDisable(GL_LINE_STIPPLE);
            glDisable(GL_LINE_SMOOTH);
        } else if (m_type==GL_POLYGON) {
            glDisable(GL_POLYGON_SMOOTH);
        }
        glDisable(GL_BLEND);
    }
}

//////////////////////////////////////////////////////////////////////////////
// gToolTip implementation

gToolTip::gToolTip(gGraphView * graphview)
    :m_graphview(graphview)
{

    m_pos.setX(0);
    m_pos.setY(0);
    m_visible=false;
    m_spacer=2; // pixels around text area
    timer=new QTimer(graphview);
    connect(timer,SIGNAL(timeout()),SLOT(timerDone()));
}

gToolTip::~gToolTip()
{
    disconnect(timer,SLOT(timerDone()));
    delete timer;
}
//void gToolTip::calcSize(QString text,int &w, int &h)
//{
    /*GetTextExtent(text,w,h);
    w+=m_spacer*2;
    h+=m_spacer*2; */
//}

void gToolTip::display(QString text, int x, int y, int timeout)
{
    m_text=text;
    m_visible=true;
    // TODO: split multiline here
    //calcSize(m_text,tw,th);

    m_pos.setX(x);
    m_pos.setY(y);

    //tw+=m_spacer*2;
    //th+=m_spacer*2;
    //th*=2;
    if (timer->isActive()) {
        timer->stop();
    }
    timer->setSingleShot(true);
    timer->start(timeout);
}

void gToolTip::cancel()
{
    m_visible=false;
    timer->stop();
}

void gToolTip::paint()     //actually paints it.
{
    if (!m_visible) return;
    int x=m_pos.x();// - tw / 2;
    int y=m_pos.y();// - th;

    /*GLBuffer * & lines=m_graphview->lines;
    GLBuffer * & quads=m_graphview->quads;
    QColor col(255,255,128,200);
    quads->add(x,y,x,y+th,col);
    quads->add(x+tw,y+th,x+tw,y,col);

    QColor blk(0,0,0,255);
    lines->add(x-1,y-1,x+tw+1,y-1,blk);
    lines->add(x-1,y+th+1,x+tw+1,y+th+1,blk);
    lines->add(x-1,y-1,x-1,y+th+1,blk);
    lines->add(x+tw+1,y-1,x+tw+1,y+th+1,blk);

    m_graphview->AddTextQue(m_text,x + m_spacer,y + m_spacer + th/2); */

    QPainter painter(m_graphview);

    QRect br;
    QRect rect(x,y,0,0);
    painter.setFont(*defaultfont);
    rect=painter.boundingRect(rect,Qt::AlignCenter,m_text);
    int w=rect.width()+m_spacer*2;
    int xx=rect.x()-m_spacer;
    if (xx<0) xx=0;
    rect.setLeft(xx);
    rect.setTop(rect.y()-rect.height()/2);
    rect.setWidth(w);

    //rect.setHeight(rect.height());
    QBrush brush(QColor(255,255,128,200));
    painter.setBrush(brush);
    int z=rect.x()+rect.width();
    if (z>m_graphview->width()-10) {
        rect.setLeft(m_graphview->width()-2-rect.width());//m_pos.x()-m_spacer);
        rect.setRight(m_graphview->width()-2);
    }
    int h=rect.height();
    if (rect.y()<0) {
        rect.setY(0);
        rect.setHeight(h);
    }

    painter.drawRoundedRect(rect,5,5);
    painter.drawText(rect,Qt::AlignCenter,m_text);

    painter.end();



}
void gToolTip::timerDone()
{
    m_visible=false;
    m_graphview->redraw();
}

Layer::Layer(ChannelID code)
{
    m_code = code;
    m_visible = true;
    m_movable = false;

    m_day=NULL;
    m_miny=m_maxy=0;
    m_minx=m_maxx=0;
    m_order=0;
    m_width=m_height=0;
    m_X=m_Y=0;
    m_position=LayerCenter;
    m_refcount=0;
}

Layer::~Layer()
{
    for (int i=0;i<mgl_buffers.size();i++) {
        delete mgl_buffers[i];
    }
}
void Layer::drawGLBuf(float linesize)
{
    int type;
    float size;
    if (!m_visible) return;
    GLBuffer *buf;
    gVertexBuffer *vb;
    for (int i=0;i<mv_buffers.size();i++) {
        vb=mv_buffers[i];
        size=vb->size();
        type=vb->type();
        if ((linesize>size) && ((type==GL_LINES) || (type==GL_LINE_LOOP))) {
            vb->setSize(linesize);
        }
        vb->draw();
        vb->setSize(size);
    }
    for (int i=0;i<mgl_buffers.size();i++) {
        buf=mgl_buffers[i];
        size=buf->size();
        type=buf->type();
        if ((linesize>size) && ((type==GL_LINES) || (type==GL_LINE_LOOP))) {
            buf->setSize(linesize);
        }
        buf->draw();
        //if ((linesize>size) && ((type==GL_LINES) || (type==GL_LINE_LOOP))) {
        buf->setSize(size);
        //}
    }
}

void Layer::SetDay(Day * d)
{
    if (d) {
        m_day=d;
        m_minx=d->first(m_code);
        m_maxx=d->last(m_code);
        m_miny=d->Min(m_code);
        m_maxy=d->Max(m_code);
    } else m_day=NULL;

}

bool Layer::isEmpty()
{
    //if (m_day && (m_day->count(m_code)>0))
    if (m_day && (m_day->channelExists(m_code)))
        return false;
    return true;
}
void Layer::setLayout(LayerPosition position, short width, short height, short order)
{
    m_position=position;
    m_width=width;
    m_height=height;
    m_order=order;
}

LayerGroup::LayerGroup() :
    Layer(NoChannel)
{
}
LayerGroup::~LayerGroup()
{
}
bool LayerGroup::isEmpty()
{
    if (!m_day)
        return true;
    bool empty=true;
    for (int i=0;i<layers.size();i++) {
        if (layers[i]->isEmpty()) {
            empty=false;
            break;
        }
    }
    return empty;
}
void LayerGroup::drawGLBuf(float linesize)
{
    Layer::drawGLBuf(linesize);
    for (int i=0;i<layers.size();i++) {
        layers[i]->drawGLBuf(linesize);
    }
}

void LayerGroup::SetDay(Day * d)
{
    m_day=d;
    for (int i=0;i<layers.size();i++) {
         layers[i]->SetDay(d);
    }
}

void LayerGroup::AddLayer(Layer *l)
{
    layers.push_back(l);
    l->addref();
}

qint64 LayerGroup::Minx()
{
    bool first=true;
    qint64 m=0,t;
    for (int i=0;i<layers.size();i++)  {
        t=layers[i]->Minx();
        if (!t) continue;
        if (first) {
            m=t;
            first=false;
        } else
        if (m>t) m=t;
    }
    return m;
}
qint64 LayerGroup::Maxx()
{
    bool first=true;
    qint64 m=0,t;
    for (int i=0;i<layers.size();i++)  {
        t=layers[i]->Maxx();
        if (!t) continue;
        if (first) {
            m=t;
            first=false;
        } else
        if (m<t) m=t;
    }
    return m;
}
EventDataType LayerGroup::Miny()
{
    bool first=true;
    EventDataType m=0,t;
    for (int i=0;i<layers.size();i++)  {
        t=layers[i]->Miny();
        if (t==layers[i]->Minx()) continue;
        if (first) {
            m=t;
            first=false;
        } else {
            if (m>t) m=t;
        }
    }
    return m;
}
EventDataType LayerGroup::Maxy()
{
    bool first=true;
    EventDataType m=0,t;
    for (int i=0;i<layers.size();i++)  {
        t=layers[i]->Maxy();
        if (t==layers[i]->Miny()) continue;
        if (first) {
            m=t;
            first=false;
        } else
        if (m<t) m=t;
    }
    return m;
}


const double zoom_hard_limit=500.0;

#ifdef ENABLE_THREADED_DRAWING

gThread::gThread(gGraphView *g)
{
    graphview=g;
    mutex.lock();
}
gThread::~gThread()
{
    if (isRunning()) {
        m_running=false;
        mutex.unlock();
        wait();
        terminate();
    }
}

void gThread::run()
{
    m_running=true;
    gGraph * g;
    while (m_running) {
        mutex.lock();
        //mutex.unlock();
        if (!m_running) break;

        do {
            g=graphview->popGraph();
            if (g) {
                g->paint(g->m_lastbounds.x(),g->m_lastbounds.y(),g->m_lastbounds.width(),g->m_lastbounds.height());
                //int i=0;
            } else {
                //mutex.lock();
                graphview->masterlock->release(1); // This thread gives up for now..

            }
        } while (g);
    }
}

#endif
gGraph::gGraph(gGraphView *graphview,QString title,QString units, int height,short group) :
    m_graphview(graphview),
    m_title(title),
    m_units(units),
    m_height(height),
    m_visible(true)
{
    m_min_height=60;
    m_layers.clear();

    if (graphview) {
        graphview->addGraph(this,group);
        timer=new QTimer(graphview);
        connect(timer,SIGNAL(timeout()),SLOT(Timeout()));
    } else {
        qWarning() << "gGraph created without a gGraphView container.. Naughty programmer!! Bad!!!";
    }
    m_margintop=14;
    m_marginbottom=5;
    m_marginleft=0;
    m_marginright=15;
    m_selecting_area=m_blockzoom=false;
    m_lastx23=0;


    m_quad=new gVertexBuffer(64,GL_QUADS);
    m_quad->forceAntiAlias(true);
    f_miny=f_maxy=0;
    m_enforceMinY=m_enforceMaxY=false;
    rec_miny=rec_maxy=0;
    m_showTitle=true;
    m_printing=false;
}
gGraph::~gGraph()
{
    for (int i=0;i<m_layers.size();i++) {
        if (m_layers[i]->unref())
            delete m_layers[i];
    }
    m_layers.clear();
    delete m_quad;

    timer->stop();
    disconnect(timer,0,0,0);
    delete timer;
}
void gGraph::Trigger(int ms)
{
    if (timer->isActive()) timer->stop();
    timer->setSingleShot(true);
    timer->start(ms);
}
void gGraph::Timeout()
{
    deselect();
    m_graphview->timedRedraw(0);
}

void gGraph::deselect()
{
    for (QVector<Layer *>::iterator l=m_layers.begin();l!=m_layers.end();l++) {
        (*l)->deselect();
    }
}
bool gGraph::isSelected()
{
    bool res=false;
    for (QVector<Layer *>::iterator l=m_layers.begin();l!=m_layers.end();l++) {
        res=(*l)->isSelected();
        if (res) break;
    }
    return res;
}

bool gGraph::isEmpty()
{
    bool empty=true;
    for (QVector<Layer *>::iterator l=m_layers.begin();l!=m_layers.end();l++) {
        if (!(*l)->isEmpty()) {
            empty=false;
            break;
        }
    }
    return empty;
}


float gGraph::printScaleX() { return m_graphview->printScaleX(); }
float gGraph::printScaleY() { return m_graphview->printScaleY(); }


void gGraph::drawGLBuf()
{

    float linesize=1;
    if (m_printing) linesize=4; //ceil(m_graphview->printScaleY());
    for (int i=0;i<m_layers.size();i++) {
        m_layers[i]->drawGLBuf(linesize);
    }
    m_quad->draw();
}
void gGraph::setDay(Day * day)
{
    m_day=day;
    for (int i=0;i<m_layers.size();i++) {
        m_layers[i]->SetDay(day);
    }
    ResetBounds();
}

void gGraph::qglColor(QColor col)
{
    m_graphview->qglColor(col);
}

void gGraph::renderText(QString text, int x,int y, float angle, QColor color, QFont *font)
{
    m_graphview->AddTextQue(text,x,y,angle,color,font);
}

void gGraph::paint(int originX, int originY, int width, int height)
{
    m_lastbounds=QRect(originX,originY,width,height);

    /*glEnable(GL_BLEND);
    glBegin(GL_QUADS);
    glColor4f(1,1,1,0.4); // Gradient End
    glVertex2i(originX,originY);
    glVertex2i(originX+width,originY);
    glColor4f(1,1,1.0,.7); // Gradient End
    glVertex2i(originX+width,originY+height);
    glVertex2i(originX,originY+height);
    glEnd();
    glDisable(GL_BLEND);
    */

    int fw,font_height;
    GetTextExtent("Wg@",fw,font_height);
    if (m_margintop>0) m_margintop=font_height+(8*printScaleY());
    //m_marginbottom=5;

    //glColor4f(0,0,0,1);
    left=marginLeft(),right=marginRight(),top=marginTop(),bottom=marginBottom();
    int x=0,y=0;
    if (m_showTitle) {
        GetTextExtent(title(),x,y,mediumfont);
        int title_x=(float(y)*2);
        renderText(title(),marginLeft()+title_x,originY+height/2,90,Qt::black,mediumfont);
        left+=title_x;
    } else left=0;

//#define DEBUG_LAYOUT
#ifdef DEBUG_LAYOUT
    QColor col=Qt::red;
    lines()->add(0,originY,0,originY+height,col);
    lines()->add(left,originY,left,originY+height,col);
#endif


    int tmp;

    left=0;

    for (int i=0;i<m_layers.size();i++) {
        Layer *ll=m_layers[i];
        if (!ll->visible()) continue;
        tmp=ll->Height()*m_graphview->printScaleY();
        if (ll->position()==LayerTop) top+=tmp;
        if (ll->position()==LayerBottom) bottom+=tmp;
    }

    for (int i=0;i<m_layers.size();i++) {
        Layer *ll=m_layers[i];
        if (!ll->visible()) continue;
        tmp=ll->Width()*m_graphview->printScaleX();
        if (ll->position()==LayerLeft) {
            ll->paint(*this,originX+left,originY+top,tmp,height-top-bottom);
            left+=tmp;
#ifdef DEBUG_LAYOUT
            lines()->add(originX+left-1,originY,originX+left-1,originY+height,col);
#endif
        }
        if (ll->position()==LayerRight) {
            right+=tmp;
            ll->paint(*this,originX+width-right,originY+top,tmp,height-top-bottom);
#ifdef DEBUG_LAYOUT
            lines()->add(originX+width-right,originY,originX+width-right,originY+height,col);
#endif
        }
    }

    bottom=marginBottom(); top=marginTop();
    for (int i=0;i<m_layers.size();i++) {
        Layer *ll=m_layers[i];
        if (!ll->visible()) continue;
        tmp=ll->Height()*m_graphview->printScaleY();
        if (ll->position()==LayerTop) {
            ll->paint(*this,originX+left,originY+top,width-left-right,tmp);
            top+=tmp;
        }
        if (ll->position()==LayerBottom) {
            bottom+=tmp;
            ll->paint(*this,originX+left,originY+height-bottom,width-left-right,tmp);
        }
    }

    for (int i=0;i<m_layers.size();i++) {
        Layer *ll=m_layers[i];
        if (!ll->visible()) continue;
        if (ll->position()==LayerCenter) {
            ll->paint(*this,originX+left,originY+top,width-left-right,height-top-bottom);
        }
    }

    if (m_selection.width()>0 && m_selecting_area) {
        QColor col(128,128,255,128);
        quads()->add(originX+m_selection.x(),originY+top, originX+m_selection.x()+m_selection.width(),originY+top,col.rgba());
        quads()->add(originX+m_selection.x()+m_selection.width(),originY+height-bottom, originX+m_selection.x(),originY+height-bottom,col.rgba());
    }
}
void gGraphView::queGraph(gGraph * g,int left, int top, int width, int height)
{
    g->m_lastbounds=QRect(left,top,width,height);
#ifdef ENABLED_THREADED_DRAWING
    dl_mutex.lock();
#endif
    m_drawlist.push_back(g);
#ifdef ENABLED_THREADED_DRAWING
    dl_mutex.unlock();
#endif
}
void gGraphView::trashGraphs()
{
    //for (int i=0;i<m_graphs.size();i++) {
        //delete m_graphs[i];
    //}
    m_graphs.clear();
    m_graphsbytitle.clear();
}
gGraph * gGraphView::popGraph()
{
    gGraph * g;
#ifdef ENABLED_THREADED_DRAWING
    dl_mutex.lock();
#endif
    if (!m_drawlist.isEmpty()) {
        g=m_drawlist.at(0);
        m_drawlist.pop_front();
    } else g=NULL;
#ifdef ENABLED_THREADED_DRAWING
    dl_mutex.unlock();
#endif
    return g;
}

void gGraph::AddLayer(Layer * l,LayerPosition position, short width, short height, short order, bool movable, short x, short y)
{
    l->setLayout(position,width,height,order);
    l->setMovable(movable);
    l->setPos(x,y);
    l->addref();
    m_layers.push_back(l);
}
void gGraph::redraw()
{
    m_graphview->redraw();
}
void gGraph::timedRedraw(int ms)
{
    m_graphview->timedRedraw(ms);
}

void gGraph::mouseMoveEvent(QMouseEvent * event)
{
   // qDebug() << m_title << "Move" << event->pos() << m_graphview->pointClicked();
    //int y=event->pos().y();
    int x=event->pos().x();
    int x2=m_graphview->pointClicked().x();//,y2=m_graphview->pointClicked().y();
    int w=m_lastbounds.width()-(right);
    //int h=m_lastbounds.height()-(bottom+m_marginbottom);
    double xx=max_x-min_x;
    double xmult=xx/w;


    //bool nolayer=false;
    bool doredraw=false;

    if (m_graphview->m_selected_graph==this) {
        if (event->buttons() & Qt::LeftButton) {
            //qDebug() << m_title << "Moved" << x << y << left << right << top << bottom << m_width << h;
            int a1=MIN(x,x2);
            int a2=MAX(x,x2);
            if (a1<left) a1=left;
            if (a2>w) a2=w;
            m_selecting_area=true;
            m_selection=QRect(a1-1,0,a2-a1,m_lastbounds.height());
            double w2=m_lastbounds.width()-right-left; //-(right+m_marginright)-(m_marginleft+left);
            if (m_blockzoom) {
                xmult=(rmax_x-rmin_x)/w2;
            } else {
                xmult=(max_x-min_x)/w2;
            }
            qint64 a=double(a2-a1)*xmult;
            float d=double(a)/86400000.0;
            int h=a/3600000;
            int m=(a/60000) % 60;
            int s=(a/1000) % 60;
            int ms(a % 1000);
            QString str;
            if (d>1) {
                str.sprintf("%1.0f days",d);
            } else {

                str.sprintf("%02i:%02i:%02i:%03i",h,m,s,ms);
            }
            if (qstatus2) {
                qstatus2->setText(str);
            }
            //m_graphview->redraw();
            //nolayer=false;
            doredraw=true;
        } else if (event->buttons() & Qt::RightButton) {
            m_graphview->setPointClicked(event->pos());
            x-=left+m_marginleft;
            x2-=left+m_marginleft;
            //int a1=MIN(x,x2);
            //int a2=MAX(x,x2);
            //if (a1<m_marginleft+left) a1=m_marginleft+left;
            //if (a2>w) a2=w;
            if (!m_blockzoom) {
                xx=max_x-min_x;
                w-=m_marginleft+left;
                xmult=xx/double(w);
                qint64 j1=xmult*x;
                qint64 j2=xmult*x2;
                qint64 jj=j2-j1;
                min_x+=jj;
                max_x+=jj;
                if (min_x<rmin_x) {
                    min_x=rmin_x;
                    max_x=rmin_x+xx;
                }
                if (max_x>rmax_x) {
                    max_x=rmax_x;
                    min_x=rmax_x-xx;
                }
                //if (a2>rmax_x) a2=rmax_x;
                m_graphview->SetXBounds(min_x,max_x,m_group,false);
                doredraw=true;
                //nolayer=true;
            } else {
                qint64 qq=rmax_x-rmin_x;
                xx=max_x-min_x;
                if (xx==qq) xx=1800000;
                w-=m_marginleft+left;
                xmult=qq/double(w);
                qint64 j1=(xmult*x);
                min_x=rmin_x+j1-(xx/2);
                max_x=min_x+xx;
                if (min_x<rmin_x) {
                    min_x=rmin_x;
                    max_x=rmin_x+xx;
                }
                if (max_x>rmax_x) {
                    max_x=rmax_x;
                    min_x=rmax_x-xx;
                }
                m_graphview->SetXBounds(min_x,max_x,m_group,false);
                doredraw=true;
                //nolayer=true;

            }
        }
    }

    //if (!nolayer) { // no mouse button
        for (int i=0;i<m_layers.size();i++) {
            if (m_layers[i]->mouseMoveEvent(event)) doredraw=true;
        }
        if (doredraw)
            m_graphview->redraw();
    //}
    //if (x>left+m_marginleft && x<m_lastbounds.width()-(right+m_marginright) && y>top+m_margintop && y<m_lastbounds.height()-(bottom+m_marginbottom)) { // main area
//        x-=left+m_marginleft;
//        y-=top+m_margintop;
//        //qDebug() << m_title << "Moved" << x << y << left << right << top << bottom << m_width << m_height;
//    }
}
void gGraph::mousePressEvent(QMouseEvent * event)
{
    for (int i=0;i<m_layers.size();i++)
        if (m_layers[i]->mousePressEvent(event)) return ;
    /*int y=event->pos().y();
    int x=event->pos().x();
    int w=m_lastbounds.width()-(right+m_marginright);
    //int h=m_lastbounds.height()-(bottom+m_marginbottom);
    //int x2,y2;
    double xx=max_x-min_x;
    //double xmult=xx/w;
    if (x>left+m_marginleft && x<m_lastbounds.width()-(right+m_marginright) && y>top+m_margintop && y<m_lastbounds.height()-(bottom+m_marginbottom)) { // main area
        x-=left+m_marginleft;
        y-=top+m_margintop;
    }*/
    //qDebug() << m_title << "Clicked" << x << y << left << right << top << bottom << m_width << m_height;
}


void gGraph::mouseReleaseEvent(QMouseEvent * event)
{
    for (int i=0;i<m_layers.size();i++)
        if (m_layers[i]->mouseReleaseEvent(event))
            return;

    int y=event->pos().y();
    int x=event->pos().x();
    int w=m_lastbounds.width()-left-right; //(m_marginleft+left+right+m_marginright);
    int h=m_lastbounds.height()-(bottom); //+m_marginbottom);
    int x2=m_graphview->pointClicked().x(),y2=m_graphview->pointClicked().y();


    //qDebug() << m_title << "Released" << min_x << max_x << x << y << x2 << y2 << left << right << top << bottom << m_width << m_height;
    if (m_selecting_area) {
        m_selecting_area=false;
        m_selection.setWidth(0);

        if (m_graphview->horizTravel()>mouse_movement_threshold) {
            x-=left;//+m_marginleft;
            y-=top;//+m_margintop;
            x2-=left;//+m_marginleft;
            y2-=top;//+m_margintop;
            if (x<0) x=0;
            if (x2<0) x2=0;
            if (x>w) x=w;
            if (x2>w) x2=w;
            double xx;
            double xmult;
            if (!m_blockzoom) {
                xx=max_x-min_x;
                xmult=xx/double(w);
                qint64 j1=min_x+xmult*x;
                qint64 j2=min_x+xmult*x2;
                qint64 a1=MIN(j1,j2)
                qint64 a2=MAX(j1,j2)
                //if (a1<rmin_x) a1=rmin_x;
                if (a2>rmax_x) a2=rmax_x;
                if (a1<=rmin_x && a2<=rmin_x) {
                    //qDebug() << "Foo??";
                } else {
                    if (a2-a1<zoom_hard_limit) a2=a1+zoom_hard_limit;
                    m_graphview->SetXBounds(a1,a2,m_group);
                }
            } else {
                xx=rmax_x-rmin_x;
                xmult=xx/double(w);
                qint64 j1=rmin_x+xmult*x;
                qint64 j2=rmin_x+xmult*x2;
                qint64 a1=MIN(j1,j2)
                qint64 a2=MAX(j1,j2)
                //if (a1<rmin_x) a1=rmin_x;
                if (a2>rmax_x) a2=rmax_x;

                if (a1<=rmin_x && a2<=rmin_x) {
                    qDebug() << "Foo2??";
                } else  {
                    if (a2-a1<zoom_hard_limit) a2=a1+zoom_hard_limit;
                    m_graphview->SetXBounds(a1,a2,m_group);
                }
            }

            return;
        } else m_graphview->redraw();
    }

    if ((m_graphview->horizTravel()<mouse_movement_threshold) && (x>left && x<w+left && y>top && y<h)) {
        // normal click in main area
        if (!m_blockzoom) {
            double zoom;
            if (event->button() & Qt::RightButton) {
                zoom=1.33;
                if (event->modifiers() & Qt::ControlModifier) zoom*=1.5;
                ZoomX(zoom,x);  // Zoom out
                return;
            } else if (event->button() & Qt::LeftButton) {
                zoom=0.75;
                if (event->modifiers() & Qt::ControlModifier) zoom/=1.5;
                ZoomX(zoom,x); // zoom in.
                return;
            }
        } else {
            x-=left;
            y-=top;
            //w-=m_marginleft+left;
            double qq=rmax_x-rmin_x;
            double xmult;

            double xx=max_x-min_x;
            //if (xx==qq) xx=1800000;

            xmult=qq/double(w);
            if ((xx==qq) || (x==m_lastx23)) {
                double zoom;
                if (event->button() & Qt::RightButton) {
                    zoom=1.33;
                    if (event->modifiers() & Qt::ControlModifier) zoom*=1.5;
                } else if (event->button() & Qt::LeftButton) {
                    zoom=0.75;
                    if (event->modifiers() & Qt::ControlModifier) zoom/=1.5;
                }
                xx*=zoom;
                if (xx<qq/zoom_hard_limit) xx=qq/zoom_hard_limit;
                if (xx>qq) xx=qq;
            }
            double j1=xmult*x;
            min_x=rmin_x+j1-(xx/2.0);
            max_x=min_x+xx;
            if (min_x<rmin_x) {
                min_x=rmin_x;
                max_x=rmin_x+xx;
            } else
            if (max_x>rmax_x) {
                max_x=rmax_x;
                min_x=rmax_x-xx;
            }
            m_graphview->SetXBounds(min_x,max_x,m_group);
            m_lastx23=x;
        }
    }
    //m_graphview->redraw();
}


void gGraph::wheelEvent(QWheelEvent * event)
{
    //qDebug() << m_title << "Wheel" << event->x() << event->y() << event->delta();
    //int y=event->pos().y();
    int x=event->pos().x()-m_graphview->titleWidth;//(left+m_marginleft);
    if (event->delta()>0) {
        ZoomX(0.75,x);
    } else {
        ZoomX(1.5,x);
    }
}
void gGraph::mouseDoubleClickEvent(QMouseEvent * event)
{
    //mousePressEvent(event);
    //mouseReleaseEvent(event);
    int y=event->pos().y();
    int x=event->pos().x();
    int w=m_lastbounds.width()-(m_marginleft+left+right+m_marginright);
    int h=m_lastbounds.height()-(bottom+m_marginbottom);
    //int x2=m_graphview->pointClicked().x(),y2=m_graphview->pointClicked().y();
    if ((m_graphview->horizTravel()<mouse_movement_threshold) && (x>left+m_marginleft && x<w+m_marginleft+left && y>top+m_margintop && y<h)) { // normal click in main area
        if (event->button() & Qt::RightButton) {
            ZoomX(1.66,x);  // Zoon out
            return;
        } else if (event->button() & Qt::LeftButton) {
            ZoomX(0.75/2.0,x); // zoom in.
            return;
        }
    }
    //mousePressEvent(event);
    //mouseReleaseEvent(event);
    //qDebug() << m_title << "Double Clicked" << event->x() << event->y();
}
void gGraph::keyPressEvent(QKeyEvent * event)
{
    for (QVector<Layer *>::iterator i=m_layers.begin();i!=m_layers.end();i++) {
        (*i)->keyPressEvent(event);
    }
    //qDebug() << m_title << "Key Pressed.. implement me" << event->key();
}

void gGraph::ZoomX(double mult,int origin_px)
{

    int width=m_lastbounds.width()-left-right; //(m_marginleft+left+right+m_marginright);
    if (origin_px==0) origin_px=(width/2); else origin_px-=left;

    if (origin_px<0) origin_px=0;
    if (origin_px>width) origin_px=width;


    // Okay, I want it to zoom in centered on the mouse click area..
    // Find X graph position of mouse click
    // find current zoom width
    // apply zoom
    // center on point found in step 1.

    qint64 min=min_x;
    qint64 max=max_x;

    double hardspan=rmax_x-rmin_x;
    double span=max-min;
    double ww=double(origin_px) / double(width);
    double origin=ww * span;
    //double center=0.5*span;
    //double dist=(origin-center);

    double q=span*mult;
    if (q>hardspan) q=hardspan;
    if (q<hardspan/zoom_hard_limit) q=hardspan/zoom_hard_limit;

    min=min+origin-(q*ww);
    max=min+q;

    if (min<rmin_x) {
        min=rmin_x;
        max=min+q;
    }
    if (max>rmax_x) {
        max=rmax_x;
        min=max-q;
    }
    m_graphview->SetXBounds(min,max,m_group);
    //updateSelectionTime(max-min);
}

void gGraph::DrawTextQue()
{
    m_graphview->DrawTextQue();
}

// margin recalcs..
void gGraph::resize(int width, int height)
{
    width=width;
    height=height;
    //m_height=height;
    //m_width=width;
}

qint64 gGraph::MinX()
{
    qint64 val=0,tmp;
    for (QVector<Layer *>::iterator l=m_layers.begin();l!=m_layers.end();l++) {
        if ((*l)->isEmpty()) continue;
        tmp=(*l)->Minx();
        if (!tmp) continue;
        if (!val || tmp < val) val = tmp;
    }
    if (val) rmin_x=val;
    return val;
}
qint64 gGraph::MaxX()
{
    //bool first=true;
    qint64 val=0,tmp;
    for (QVector<Layer *>::iterator l=m_layers.begin();l!=m_layers.end();l++) {
        if ((*l)->isEmpty()) continue;
        tmp=(*l)->Maxx();
        //if (!tmp) continue;
        if (!val || tmp > val) val = tmp;
    }
    if (val) rmax_x=val;
    return val;
}
EventDataType gGraph::MinY()
{
    bool first=true;
    EventDataType val=0,tmp;
    if (m_enforceMinY) return rmin_y=f_miny;
    for (QVector<Layer *>::iterator l=m_layers.begin();l!=m_layers.end();l++) {
        if ((*l)->isEmpty()) continue;
        tmp=(*l)->Miny();
        if (tmp==0 && tmp==(*l)->Maxy())
            continue;
        if (first) {
            val=tmp;
            first=false;
        } else {
            if (tmp < val) val = tmp;
        }
    }
    return rmin_y=val;
}
EventDataType gGraph::MaxY()
{
    bool first=true;
    EventDataType val=0,tmp;
    if (m_enforceMaxY) return rmax_y=f_maxy;
    for (QVector<Layer *>::iterator l=m_layers.begin();l!=m_layers.end();l++) {
        if ((*l)->isEmpty()) continue;
        tmp=(*l)->Maxy();
        if (tmp==0 && tmp==(*l)->Miny()) continue;
        if (first) {
            val=tmp;
            first=false;
        } else {
            if (tmp > val) val = tmp;
        }
    }
    return rmax_y=val;
}

void gGraph::SetMinX(qint64 v)
{
    rmin_x=min_x=v;
}
void gGraph::SetMaxX(qint64 v)
{
    rmax_x=max_x=v;
}
void gGraph::SetMinY(EventDataType v)
{
    rmin_y=min_y=v;
}
void gGraph::SetMaxY(EventDataType v)
{
    rmax_y=max_y=v;
}
gVertexBuffer * gGraph::lines()
{
    return m_graphview->lines;
}
gVertexBuffer * gGraph::backlines()
{
    return m_graphview->backlines;
}
gVertexBuffer * gGraph::quads()
{
    return m_graphview->quads;
}
//GLShortBuffer * gGraph::stippled()
//{
//    return m_graphview->stippled;
//}
//gVertexBuffer * gGraph::vlines()
//{ return m_graphview->vlines; } // testing new vertexbuffer

short gGraph::marginLeft() { return m_marginleft; }//*m_graphview->printScaleX(); }
short gGraph::marginRight() { return m_marginright; } //*m_graphview->printScaleX(); }
short gGraph::marginTop() { return m_margintop; } //*m_graphview->printScaleY(); }
short gGraph::marginBottom() { return m_marginbottom; } //*m_graphview->printScaleY(); }

Layer * gGraph::getLineChart()
{
    gLineChart *lc;
    for (int i=0;i<m_layers.size();i++) {
        lc=dynamic_cast<gLineChart *>(m_layers[i]);
        if (lc) return lc;
    }
    return NULL;
}

QPixmap gGraph::renderPixmap(int w, int h, bool printing)
{

    gGraphView *sg=mainwin->snapshotGraph();
    if (!sg) return QPixmap();

    QFont * _defaultfont=defaultfont;
    QFont * _mediumfont=mediumfont;
    QFont * _bigfont=bigfont;

    QFont fa=*defaultfont;
    QFont fb=*mediumfont;
    QFont fc=*bigfont;


    m_printing=printing;
    if (printing) {
        fa.setPixelSize(30);
        fb.setPixelSize(35);
        fc.setPixelSize(80);
        sg->setPrintScaleX(3);
        sg->setPrintScaleY(3);
    } else {
        sg->setPrintScaleX(1);
        sg->setPrintScaleY(1);
    }
    defaultfont=&fa;
    mediumfont=&fb;
    bigfont=&fc;

    sg->hideSplitter();
    gGraphView *tgv=m_graphview;
    m_graphview=sg;

    sg->setMinimumSize(w,h);
    sg->setMaximumSize(w,h);
    sg->setFixedSize(w,h);

    float tmp=m_height;
    m_height=h;
    sg->trashGraphs();
    sg->addGraph(this);

    sg->setScaleY(1.0);

    QPixmap pm=sg->renderPixmap(w,h,false);

    sg->trashGraphs();
    m_graphview=tgv;

    m_height=tmp;

    defaultfont=_defaultfont;
    mediumfont=_mediumfont;
    bigfont=_bigfont;
    m_printing=false;

    return pm;
}

// Sets a new Min & Max X clipping, refreshing the graph and all it's layers.
void gGraph::SetXBounds(qint64 minx, qint64 maxx)
{
    min_x=minx;
    max_x=maxx;

    //repaint();
    //m_graphview->redraw();
}
int gGraph::flipY(int y)
{
    return m_graphview->height()-y;
}

void gGraph::ResetBounds()
{
    min_x=MinX();
    max_x=MaxX();
    min_y=MinY();
    max_y=MaxY();
}
void gGraph::ToolTip(QString text, int x, int y, int timeout)
{
    m_graphview->m_tooltip->display(text,x,y,timeout);
}

void gGraph::roundY(EventDataType &miny, EventDataType &maxy)
{
    int m,t;
    bool ymin_good=false,ymax_good=false;
    if (rec_miny!=rec_maxy) {
        if (miny>rec_miny)
            miny=rec_miny;
        if (maxy<rec_maxy)
            maxy=rec_maxy;

        if (miny==rec_miny)
            ymin_good=true;
        if (maxy==rec_maxy)
            ymax_good=true;
    }
    if (maxy==miny) {
        m=ceil(maxy/2.0);
        t=m*2;
        if (maxy==t)
            t+=2;
        if (!ymax_good)
            maxy=t;

        m=floor(miny/2.0);
        t=m*2;
        if (miny==t)
            t-=2;
        if (miny>=0 && t<0)
            t=0;
        if (!ymin_good)
            miny=t;
        return;
    }

    if (maxy>=400) {
        m=ceil(maxy/50.0);
        t=m*50;
        if (!ymax_good)
            maxy=t;
        m=floor(miny/50.0);
        if (!ymin_good)
            miny=m*50;
    } else if (maxy>=5) {
        m=ceil(maxy/5.0);
        t=m*5;
        if (!ymax_good)
            maxy=t;
        m=floor(miny/5.0);
        if (!ymin_good)
            miny=m*5;
    } else {
        if (maxy==miny && maxy==0) {
            maxy=0.5;
        } else {
            //maxy*=4.0;
            //miny*=4.0;
            if (!ymax_good)
                maxy=ceil(maxy);
            if (!ymin_good)
                miny=floor(miny);
            //maxy/=4.0;
            //miny/=4.0;
        }
    }

    //if (m_enforceMinY) { miny=f_miny; }
    //if (m_enforceMaxY) { maxy=f_maxy; }
}

gGraphView::gGraphView(QWidget *parent, gGraphView * shared) :
    QGLWidget(parent,shared),
    m_offsetY(0),m_offsetX(0),m_scaleY(1.0),m_scrollbar(NULL)
{
    m_shared=shared;
    m_sizer_index=m_graph_index=0;
    m_textque_items=0;
    m_button_down=m_graph_dragging=m_sizer_dragging=false;
    m_lastypos=m_lastxpos=0;
    m_horiz_travel=0;
    this->setMouseTracking(true);
    m_emptytext=QObject::tr("No Data");
    InitGraphs();
#ifdef ENABLE_THREADED_DRAWING
    m_idealthreads=QThread::idealThreadCount();
    if (m_idealthreads<=0) m_idealthreads=1;
    masterlock=new QSemaphore(m_idealthreads);
#endif
    m_tooltip=new gToolTip(this);
    /*for (int i=0;i<m_idealthreads;i++) {
        gThread * gt=new gThread(this);
        m_threads.push_back(gt);
        //gt->start();
    }*/

    lines=new gVertexBuffer(100000,GL_LINES); // big fat shared line list
    backlines=new gVertexBuffer(10000,GL_LINES); // big fat shared line list
    quads=new gVertexBuffer(1024,GL_QUADS); // big fat shared line list
    quads->forceAntiAlias(true);
    frontlines=new gVertexBuffer(20000,GL_LINES);

    //vlines=new gVertexBuffer(20000,GL_LINES);

    //stippled->setSize(1.5);
    //stippled->forceAntiAlias(false);
    //lines->setSize(1.5);
    //backlines->setSize(1.5);

    setFocusPolicy(Qt::StrongFocus);
    m_showsplitter=true;
    timer=new QTimer(this);
    connect(timer,SIGNAL(timeout()),SLOT(refreshTimeout()));
    print_scaleY=print_scaleX=1.0;

    redrawtimer=new QTimer(this);
    //redrawtimer->setInterval(80);
    //redrawtimer->start();
    connect(redrawtimer,SIGNAL(timeout()),SLOT(updateGL()));

    //cubeimg.push_back(images["brick"]);
    //cubeimg.push_back(images[""]);

    m_fadingOut=false;
    m_fadingIn=false;
    m_inAnimation=false;
    m_limbo=false;
}

gGraphView::~gGraphView()
{
#ifdef ENABLE_THREADED_DRAWING
    for (int i=0;i<m_threads.size();i++) {
        delete m_threads[i];
    }
    delete masterlock;
#endif
    for (int i=0;i<m_graphs.size();i++) {
        delete m_graphs[i];
    }
    delete m_tooltip;
    m_graphs.clear();
    //delete vlines;
    //delete stippled;
    delete frontlines;
    delete lines;
    delete backlines;
    delete quads;
    if (m_scrollbar) {
        this->disconnect(SIGNAL(sliderMoved(int)),this);
    }
    disconnect(redrawtimer,0,0,0);
    disconnect(timer,0,0,0);
    timer->stop();
    delete timer;
}
void gGraphView::DrawTextQue()
{
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    int w,h;
    QPainter painter;
#ifndef USE_RENDERTEXT
    painter.begin(this);
#endif
    for (int i=0;i<m_textque_items;i++) {
        // GL Font drawing is ass in Qt.. :(
        TextQue & q=m_textque[i];
#ifndef USE_RENDERTEXT
        QBrush b(q.color);
        painter.setBrush(b);
        painter.setFont(*q.font);
#endif

        if (q.angle==0) {
            qglColor(q.color);

            // *********************************************************
            // Holy crap this is slow
            // The following line is responsible for 77% of drawing time
            // *********************************************************
#ifdef USE_RENDERTEXT
            renderText(q.x,q.y,q.text,*q.font);
#else
            painter.drawText(q.x, q.y, q.text);
#endif
        } else {
#ifdef USE_RENDERTEXT
            painter.begin(this);
            QBrush b(q.color);
            painter.setBrush(b);
            painter.setFont(*q.font);
#endif
            w=painter.fontMetrics().width(q.text);
            h=painter.fontMetrics().xHeight()+2;

            painter.translate(q.x, q.y);
            painter.rotate(-q.angle);
            painter.drawText(floor(-w/2.0), floor(-h/2.0), q.text);
            painter.rotate(+q.angle);
            painter.translate(-q.x, -q.y);
#ifdef USE_RENDERTEXT
            painter.end();
#endif
        }
        q.text.clear();
        //q.text.squeeze();
    }
#ifndef USE_RENDERTEXT
    painter.end();
#endif
    glPopAttrib();
    //qDebug() << "rendered" << m_textque_items << "text items";
    m_textque_items=0;
}

void gGraphView::AddTextQue(QString & text, short x, short y, float angle, QColor color, QFont * font)
{
#ifdef ENABLED_THREADED_DRAWING
    text_mutex.lock();
#endif
    if (m_textque_items>=textque_max) {
        DrawTextQue();
    }
    TextQue & q=m_textque[m_textque_items++];
#ifdef ENABLED_THREADED_DRAWING
    text_mutex.unlock();
#endif
    q.text=text;
    q.x=x;
    q.y=y;
    q.angle=angle;
    q.color=color;
    q.font=font;
}

void gGraphView::addGraph(gGraph *g,short group)
{
    if (!g) {
        qDebug() << "Attempted to add an empty graph!";
        return;
    }
    if (!m_graphs.contains(g)) {
        g->setGroup(group);
        m_graphs.push_back(g);
        if (!m_graphsbytitle.contains(g->title())) {
            m_graphsbytitle[g->title()]=g;
        } else {
            qDebug() << "Can't have to graphs with the same title in one GraphView!!";
        }
       // updateScrollBar();
    }
}
float gGraphView::totalHeight()
{
    float th=0;
    for (int i=0;i<m_graphs.size();i++) {
        if (m_graphs[i]->isEmpty() || (!m_graphs[i]->visible())) continue;
        th += m_graphs[i]->height() + graphSpacer;
    }
    return ceil(th);
}
float gGraphView::findTop(gGraph * graph)
{
    float th=-m_offsetY;
    for (int i=0;i<m_graphs.size();i++) {
        if (m_graphs[i]==graph) break;
        if (m_graphs[i]->isEmpty() || (!m_graphs[i]->visible())) continue;
        th += m_graphs[i]->height()*m_scaleY + graphSpacer;
    }
    //th-=m_offsetY;
    return ceil(th);
}
float gGraphView::scaleHeight()
{
    float th=0;
    for (int i=0;i<m_graphs.size();i++) {
        if (m_graphs[i]->isEmpty() || (!m_graphs[i]->visible())) continue;
        th += m_graphs[i]->height() * m_scaleY + graphSpacer;
    }
    return ceil(th);
}
void gGraphView::resizeEvent(QResizeEvent *e)
{
    QGLWidget::resizeEvent(e); // This ques a redraw event..

    updateScale();
    for (int i=0;i<m_graphs.size();i++) {
        m_graphs[i]->resize(e->size().width(),m_graphs[i]->height()*m_scaleY);
    }
}
void gGraphView::scrollbarValueChanged(int val)
{
    //qDebug() << "Scrollbar Changed" << val;
    if (m_offsetY!=val) {
        m_offsetY=val;
        redraw(); // do this on a timer?
    }
}

void gGraphView::selectionTime()
{
    qint64 xx=m_maxx - m_minx;
    double d=xx/86400000L;
    int h=xx/3600000L;
    int m=(xx/60000) % 60;
    int s=(xx/1000) % 60;
    int ms(xx % 1000);
    QString str;
    if (d>1) {
        /*QDate d1=QDateTime::fromTime_t(m_minx/1000).toUTC().date();
        QDate d2=QDateTime::fromTime_t(m_maxx/1000).toUTC().date();
        d=PROFILE.countDays(MT_CPAP,d1,d2); */

        str.sprintf("%1.0f days",ceil(d));
    } else {
        str.sprintf("%02i:%02i:%02i:%03i",h,m,s,ms);
    }
    if (qstatus2) {
        qstatus2->setText(str);
    }

}
void gGraphView::GetRXBounds(qint64 & st, qint64 & et)
{
    //qint64 m1=0,m2=0;
    gGraph *g=NULL;
    for (int i=0;i<m_graphs.size();i++) {
        g=m_graphs[i];
        if (g->group()==0)
            break;
    }
    st=g->rmin_x;
    et=g->rmax_x;
}
void gGraphView::ResetBounds(bool refresh) //short group)
{
    Q_UNUSED(refresh)
    qint64 m1=0,m2=0;
    gGraph *g=NULL;
    for (int i=0;i<m_graphs.size();i++) {
        m_graphs[i]->ResetBounds();
        if (!m_graphs[i]->min_x) continue;
        g=m_graphs[i];
        if (!m1 || m_graphs[i]->min_x<m1) m1=m_graphs[i]->min_x;
        if (!m2 || m_graphs[i]->max_x>m2) m2=m_graphs[i]->max_x;
    }

    if (PROFILE.general->linkGroups()) {
        for (int i=0;i<m_graphs.size();i++) {
            m_graphs[i]->SetMinX(m1);
            m_graphs[i]->SetMaxX(m2);
        }
    }
    if (!g) g=m_graphs[0];

    m_minx=g->min_x;
    m_maxx=g->max_x;

    qint64 xx=g->max_x - g->min_x;
    double d=xx/86400000L;
    int h=xx/3600000L;
    int m=(xx/60000) % 60;
    int s=(xx/1000) % 60;
    int ms(xx % 1000);
    QString str;
    if (d>1) {
        /*QDate d1=QDateTime::fromTime_t(m_minx/1000).toUTC().date();
        QDate d2=QDateTime::fromTime_t(m_maxx/1000).toUTC().date();
        d=PROFILE.countDays(MT_CPAP,d1,d2); */

        str.sprintf("%1.0f days",ceil(d));
    } else {
        str.sprintf("%02i:%02i:%02i:%03i",h,m,s,ms);
    }
    if (qstatus2) {
        qstatus2->setText(str);
    }
    updateScale();
}
void gGraphView::GetXBounds(qint64 & st,qint64 & et)
{
    st=m_minx;
    et=m_maxx;
}

void gGraphView::SetXBounds(qint64 minx, qint64 maxx,short group,bool refresh)
{
    for (int i=0;i<m_graphs.size();i++) {
        if (PROFILE.general->linkGroups() || (m_graphs[i]->group()==group)) {
            m_graphs[i]->SetXBounds(minx,maxx);
        }
    }
    m_minx=minx;
    m_maxx=maxx;

    qint64 xx=maxx-minx;
    double d=xx/86400000L;
    int h=xx/3600000L;
    int m=(xx/60000) % 60;
    int s=(xx/1000) % 60;
    int ms(xx % 1000);
    QString str;

    if (d>1) {
        str.sprintf("%1.0f days",ceil(xx/86400000.0));
    } else {
        str.sprintf("%02i:%02i:%02i:%03i",h,m,s,ms);
    }
    if (qstatus2) {
        qstatus2->setText(str);
    }

    if (refresh) redraw();
}
void gGraphView::updateScale()
{
    float th=totalHeight(); // height of all graphs
    float h=height();       // height of main widget

    if (th < h) {
        m_scaleY=h / th;    // less graphs than fits on screen, so scale to fit
    } else {
        m_scaleY=1.0;
    }
    updateScrollBar();
}

void gGraphView::updateScrollBar()
{
    if (!m_scrollbar) return;

    if (!m_graphs.size()) return;

    float th=scaleHeight(); // height of all graphs
    float h=height();       // height of main widget

    float vis=0;
    for (int i=0;i<m_graphs.size();i++) vis+=m_graphs[i]->isEmpty()  || (!m_graphs[i]->visible()) ? 0 : 1;

    //vis+=1;
    if (th<h) { // less graphs than fits on screen

        m_scrollbar->setMaximum(0); // turn scrollbar off.

    } else {  // more graphs than fit on screen
        //m_scaleY=1.0;
        float avgheight=th/vis;
        m_scrollbar->setPageStep(avgheight);
        m_scrollbar->setSingleStep(avgheight/8.0);
        m_scrollbar->setMaximum(th-height());
        if (m_offsetY>th-height()) {
            m_offsetY=th-height();
        }
    }
}

void gGraphView::setScrollBar(MyScrollBar *sb)
{
    m_scrollbar=sb;
    m_scrollbar->setMinimum(0);
    updateScrollBar();
    this->connect(m_scrollbar,SIGNAL(valueChanged(int)),SLOT(scrollbarValueChanged(int)));
}
void gGraphView::initializeGL()
{
    setAutoFillBackground(false);
    setAutoBufferSwap(false);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);

    if (cubeimg.size()>0) {
        cubetex=bindTexture(*cubeimg[0]);
    }
//    texid.resize(images.size());
//    for(int i=0;i<images.size();i++) {
//        texid[i]=bindTexture(*images[i]);
//    }

    glBindTexture(GL_TEXTURE_2D,0);
   // glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
}

void gGraphView::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void gGraphView::renderSomethingFun(float alpha)
{
    if (cubeimg.size()==0) return;
//    glPushMatrix();
    float w=width();
    float h=height();
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0f,(GLfloat)w/(GLfloat)h,0.1f,100.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /*glShadeModel(GL_SMOOTH);
    glClearColor(0.0f, 0.0f, 0.0f, 0.5f);
    glClearDepth(1.0f); */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

    // This code has been shamelessly pinched of the interwebs..
    // When I'm feeling more energetic, I'll change it to a textured sheep or something.
    static float rotqube=0;

    static float xpos=0,ypos=7;

    glLoadIdentity();

    glAlphaFunc(GL_GREATER,0.1);
    glEnable(GL_ALPHA_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_COLOR_MATERIAL);

    //int imgcount=cubeimg.size();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    double xx=0.0,yy=0.0;

    // set this to 0 to make the cube stay in the center of the screen
    if (1) {
        xx=sin(M_PI/180.0 * xpos)*2; // ((4.0/width()) * m_mouse.rx())-2.0;
        yy=cos(M_PI/180.0 * ypos)*2; //2-((4.0/height()) * m_mouse.ry());
        xpos+=1;
        ypos+=1.32;
        if (xpos > 360) xpos-=360.0;
        if (ypos > 360) ypos-=360.0;
    }


    //m_mouse.x();
    glTranslatef(xx, 0.0f,-7.0f+yy);
    glRotatef(rotqube,0.0f,1.0f,0.0f);
    glRotatef(rotqube,1.0f,1.0f,1.0f);


    int i=0;
    glEnable(GL_TEXTURE_2D);
    cubetex=bindTexture(*cubeimg[0]);

    //glBindTexture(GL_TEXTURE_2D, cubetex); //texid[i % imgcount]);
    i++;
    glColor4f(1,1,1,alpha);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);	// Bottom Left Of The Texture and Quad
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);	// Bottom Right Of The Texture and Quad
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f,  1.0f);	// Top Right Of The Texture and Quad
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f,  1.0f);	// Top Left Of The Texture and Quad
    glEnd();
    // Back Face
    //bindTexture(*cubeimg[i % imgcount]);
    //glBindTexture(GL_TEXTURE_2D, texid[i % imgcount]);
    i++;
    glBegin(GL_QUADS);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -1.0f);	// Bottom Right Of The Texture and Quad
    glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);	// Top Right Of The Texture and Quad
    glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);	// Top Left Of The Texture and Quad
    glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f, -1.0f);	// Bottom Left Of The Texture and Quad
    glEnd();
    // Top Face
    //bindTexture(*cubeimg[i % imgcount]);
//    glBindTexture(GL_TEXTURE_2D, texid[i % imgcount]);
    i++;
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);	// Top Left Of The Texture and Quad
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f,  1.0f,  1.0f);	// Bottom Left Of The Texture and Quad
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f,  1.0f,  1.0f);	// Bottom Right Of The Texture and Quad
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);	// Top Right Of The Texture and Quad
    glEnd();
    // Bottom Face
    //bindTexture(*cubeimg[i % imgcount]);
    //glBindTexture(GL_TEXTURE_2D, texid[i % imgcount]);
    i++;
    glBegin(GL_QUADS);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f, -1.0f, -1.0f);	// Top Right Of The Texture and Quad
    glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f, -1.0f, -1.0f);	// Top Left Of The Texture and Quad
    glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);	// Bottom Left Of The Texture and Quad
    glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);	// Bottom Right Of The Texture and Quad
    glEnd();
    // Right face
    //bindTexture(*cubeimg[i % imgcount]);
//    glBindTexture(GL_TEXTURE_2D, texid[i % imgcount]);
    i++;
    glBegin(GL_QUADS);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, -1.0f);	// Bottom Right Of The Texture and Quad
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);	// Top Right Of The Texture and Quad
    glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f,  1.0f,  1.0f);	// Top Left Of The Texture and Quad
    glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);	// Bottom Left Of The Texture and Quad
    glEnd();
    // Left Face
    //GLuint tex=bindTexture(*images["mask"]);
    //glBindTexture(GL_TEXTURE_2D, tex);
    i++;
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -1.0f);	// Bottom Left Of The Texture and Quad
    glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);	// Bottom Right Of The Texture and Quad
    glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f,  1.0f,  1.0f);	// Top Right Of The Texture and Quad
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);	// Top Left Of The Texture and Quad
    glEnd();

    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);

    glDisable(GL_DEPTH_TEST);

    rotqube +=0.9f;

    // Restore boring 2D reality..
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    glOrtho(0, width(), height(), 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

  //  glPopMatrix();
}

bool gGraphView::renderGraphs()
{
//    glEnable(GL_BLEND);
//    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//    glBegin(GL_QUADS);
//    glColor4f(1.0,1.0,1.0,1.0); // Gradient start
//    glVertex2f(0, height());
//    glVertex2f(0, 0);
//    glColor4f(0.9,0.9,0.9,1.0); // Gradient End
//    glVertex2f(width(), 0);
//    glVertex2f(width(), height());
//    glEnd();
//    glDisable(GL_BLEND);

    float px=m_offsetX;
    float py=-m_offsetY;
    int numgraphs=0;
    float h,w;
    //ax=px;//-m_offsetX;

    //bool threaded;

    // Tempory hack using this pref..
//#ifdef ENABLED_THREADED_DRAWING
    /*if (profile->session->multithreading()) { // && (m_idealthreads>1)) {
        threaded=true;
        for (int i=0;i<m_idealthreads;i++) {
            if (!m_threads[i]->isRunning())
                m_threads[i]->start();
        }
    } else threaded=false; */
    //#endif
    //threaded=false;
    for (int i=0;i<m_graphs.size();i++) {
        if (m_graphs[i]->isEmpty()) continue;
        if (!m_graphs[i]->visible()) continue;
        numgraphs++;
        h=m_graphs[i]->height() * m_scaleY;

        // set clipping?

        if (py > height())
            break; // we are done.. can't draw anymore

        if ((py + h + graphSpacer) >= 0) {
            w=width();
            int tw=(m_graphs[i]->showTitle() ? titleWidth : 0);

            queGraph(m_graphs[i],px+tw,py,width()-tw,h);

            if (m_showsplitter) {
                // draw the splitter handle
                QColor ca=QColor(128,128,128,255);
                backlines->add(0, py+h, w, py+h, ca.rgba());
                ca=QColor(192,192,192,255);
                backlines->add(0, py+h+1, w, py+h+1, ca.rgba());
                ca=QColor(90,90,90,255);
                backlines->add(0, py+h+2, w, py+h+2, ca.rgba());
            }

        }
        py=ceil(py+h+graphSpacer);
    }

    //int thr=m_idealthreads;
#ifdef ENABLED_THREADED_DRAWING
        for (int i=0;i<m_idealthreads;i++) {
            masterlock->acquire(1);
            m_threads[i]->mutex.unlock();
        }

        // wait till all the threads are done
        // ask for all the CPU's back..
        masterlock->acquire(m_idealthreads);
        masterlock->release(m_idealthreads);

    } else { // just do it here
#endif
        int s=m_drawlist.size();
        for (int i=0;i<s;i++) {
            gGraph *g=m_drawlist.at(0);
            m_drawlist.pop_front();
            g->paint(g->m_lastbounds.x(), g->m_lastbounds.y(), g->m_lastbounds.width(), g->m_lastbounds.height());
        }
#ifdef ENABLED_THREADED_DRAWING
    }
#endif
    //int elapsed=time.elapsed();
    //QColor col=Qt::black;

    backlines->draw();
    for (int i=0;i<m_graphs.size();i++) {
        m_graphs[i]->drawGLBuf();
    }
    quads->draw();
    lines->draw();
//    lines->setSize(linesize);

    DrawTextQue();
    m_tooltip->paint();
    //glDisable(GL_TEXTURE_2D);
    //glDisable(GL_DEPTH_TEST);

    return numgraphs>0;
}
void gGraphView::fadeOut()
{
    if (!PROFILE.ExistsAndTrue("AnimationsAndTransitions")) return;
    //if (m_fadingOut) {
//        return;
//    }
    //if (m_inAnimation) {
//        m_inAnimation=false;
  //  }
    //clone graphs to shapshot graphview object, render, and then fade in, before switching back to normal mode
    /*gGraphView *sg=mainwin->snapshotGraph();
    sg->trashGraphs();
    sg->setFixedSize(width(),height());
    sg->m_graphs=m_graphs;
    sg->showSplitter(); */

    //bool restart=false;
    //if (!m_inAnimation)
      //  restart=true;

    bool b=m_inAnimation;
    m_inAnimation=false;

    previous_day_snapshot=renderPixmap(width(),height(),false);
    m_inAnimation=b;
    //m_fadingOut=true;
    //m_fadingIn=false;
    //m_inAnimation=true;
    //m_limbo=false;
    //m_animationStarted.start();
  //  updateGL();
}
void gGraphView::fadeIn(bool dir)
{
    static bool firstdraw=true;
    m_tooltip->cancel();
    if (firstdraw || !PROFILE.ExistsAndTrue("AnimationsAndTransitions")) {
        updateGL();
        firstdraw=false;
        return;
    }

    if (m_fadingIn) {
        m_fadingIn=false;
        m_inAnimation=false;
        updateGL();
        return;
       // previous_day_snapshot=current_day_snapshot;
    }
    m_inAnimation=false;
    current_day_snapshot=renderPixmap(width(),height(),false);
//    qDebug() << current_day_snapshot.depth() << "bit image depth";
//    if (current_day_snapshot.hasAlpha()){
//        qDebug() << "Snapshots are not storing alpha channel needed for texture blending";
//    }
    m_inAnimation=true;

    m_animationStarted.start();
    m_fadingIn=true;
    m_limbo=false;
    m_fadedir=dir;
    updateGL();

}

void gGraphView::paintGL()
{
#ifdef DEBUG_EFFICIENCY
    QElapsedTimer time;
    time.start();
#endif

    if (redrawtimer->isActive()) {
        redrawtimer->stop();
    }

    bool something_fun=PROFILE.ExistsAndTrue("AnimationsAndTransitions");
    if (width()<=0) return;
    if (height()<=0) return;

    glClearColor(255,255,255,255);
    //glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    bool numgraphs=true;
    const int animTimeout=200;
    float phase=0;

    int elapsed=0;
    if (m_inAnimation || m_fadingIn) {
        elapsed=m_animationStarted.elapsed();

        if (elapsed > animTimeout) {
            if (m_fadingOut) {
                m_fadingOut=false;
                m_animationStarted.start();
                elapsed=0;
                m_limbo=true;
            } else if (m_fadingIn) {
                m_fadingIn=false;
                m_inAnimation=false;  // end animation
                m_limbo=false;
                m_fadingOut=false;
            }
//
        } else {
            phase=float(elapsed) / float(animTimeout); //percentage of way through animation timeslot
            if (phase>1.0) phase=1.0;
            if (phase<0) phase=0;
        }

        if (m_inAnimation) {
            if (m_fadingOut) {
              //  bindTexture(previous_day_snapshot);
            } else if (m_fadingIn) {
                //int offset,offset2;
                float aphase;
                aphase=1.0-phase;
                /*if (m_fadedir) { // forwards
                    //offset2=-width();
                    //offset=0;
                    aphase=phase;
                    phase=1.0-phase;
                } else { // backwards
                    aphase=phase;
                    phase=phase
                    //offset=-width();
                    //offset2=0;//-width();
                }*/
                //offset=0; offset2=0;

                glEnable(GL_BLEND);

                glDisable(GL_ALPHA_TEST);
                glAlphaFunc(GL_GREATER,0.0);
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                glColor4f(aphase,aphase,aphase,aphase);

                bindTexture(previous_day_snapshot);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(0,0);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(width(),0);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(width(),height());
                glTexCoord2f(0.0f, 0.0f); glVertex2f(0,height());
                glEnd();

                glColor4f(phase,phase,phase,phase);
  //              glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                bindTexture(current_day_snapshot);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(0,0);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(width(),0);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(width(),height());
                glTexCoord2f(0.0f, 0.0f); glVertex2f(0,height());
                glEnd();

                glDisable(GL_ALPHA_TEST);
                glDisable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glBindTexture(GL_TEXTURE_2D,0);
            }
        }
    }

    // Need a really good condition/excuse to switch this on.. :-}
    bool bereallyannoying=false;

    if (!m_inAnimation || (!m_fadingIn)) {
        // Not in animation sequence, draw graphs like normal
        if (bereallyannoying)
            renderSomethingFun(0.7);

        numgraphs=renderGraphs();

        if (!numgraphs) { // No graphs drawn?
            int x,y;
            GetTextExtent(m_emptytext,x,y,bigfont);
            int tp;
            if (something_fun && this->isVisible()) {// Do something fun instead
                if (!bereallyannoying)
                    renderSomethingFun();
                tp=height()-(y/2);
            } else {
                tp=height() / 2 + y / 2;
            }
                // Then display the empty text message
            QColor col=Qt::black;
            AddTextQue(m_emptytext,(width()/2)-x/2,tp,0.0,col,bigfont);
            DrawTextQue();

        }
    }

#ifdef DEBUG_EFFICIENCY
    const int rs=10;
    static double ring[rs]={0};
    static int rp=0;

    // Show FPS and draw time
    if (m_showsplitter && PROFILE.general->showDebug()) {
        QString ss;
        qint64 ela=time.nsecsElapsed();
        double ms=double(ela)/1000000.0;
        ring[rp++]=1000.0/ms;
        rp %= rs;
        double v=0;
        for (int i=0;i<rs;i++) {
            v+=ring[i];
        }
        double fps=v/double(rs);
        ss="Debug Mode "+QString::number(ms,'f',1)+"ms ("+QString::number(fps,'f',1)+"fps)";
        int w,h;
        GetTextExtent(ss,w,h);
        QColor col=Qt::white;
        quads->add(width()-m_graphs[0]->marginRight(),0,width()-m_graphs[0]->marginRight(),w,width(),w,width(),0,col.rgba());
        quads->draw();
        AddTextQue(ss,width()+3,w/2,90,col,defaultfont);
        DrawTextQue();
    }
#endif

    swapBuffers(); // Dump to screen.

    if (this->isVisible()) {
        if (m_limbo || m_inAnimation || (something_fun && (bereallyannoying || !numgraphs))) {
            redrawtimer->setInterval(1000.0/50);
            redrawtimer->setSingleShot(true);
            redrawtimer->start();
        }
    }
}

void gGraphView::setCubeImage(QImage *img)
{
    cubeimg.clear();
    cubeimg.push_back(img);

    cubetex=bindTexture(*img);
    glBindTexture(GL_TEXTURE_2D,0);
}


// For manual scrolling
void gGraphView::setOffsetY(int offsetY)
{
    if (m_offsetY!=offsetY) {
        m_offsetY=offsetY;
        redraw(); //issue full redraw..
    }
}

// For manual X scrolling (not really needed)
void gGraphView::setOffsetX(int offsetX)
{
    if (m_offsetX!=offsetX) {
        m_offsetX=offsetX;
        redraw(); //issue redraw
    }
}

void gGraphView::mouseMoveEvent(QMouseEvent * event)
{
    int x=event->x();
    int y=event->y();

    m_mouse=QPoint(x,y);

    if (m_sizer_dragging) { // Resize handle being dragged
        float my=y - m_sizer_point.y();
        //qDebug() << "Sizer moved vertically" << m_sizer_index << my*m_scaleY;
        float h=m_graphs[m_sizer_index]->height();
        h+=my / m_scaleY;
        if (h > m_graphs[m_sizer_index]->minHeight()) {
            m_graphs[m_sizer_index]->setHeight(h);
            m_sizer_point.setX(x);
            m_sizer_point.setY(y);
            updateScrollBar();
            redraw();
        }
        return;
    }

    if (m_graph_dragging) { // Title bar being dragged to reorder
        gGraph *p;
        int yy=m_sizer_point.y();
        bool empty;
        if (y < yy) {

            for (int i=m_graph_index-1;i>=0;i--) {
                empty=m_graphs[i]->isEmpty() || (!m_graphs[i]->visible());
                // swapping upwards.
                int yy2=yy-graphSpacer-m_graphs[i]->height()*m_scaleY;
                yy2+=m_graphs[m_graph_index]->height()*m_scaleY;
                if (y<yy2) {
                    //qDebug() << "Graph Reorder" << m_graph_index;
                    p=m_graphs[m_graph_index];
                    m_graphs[m_graph_index]=m_graphs[i];
                    m_graphs[i]=p;
                    if (!empty) {
                        m_sizer_point.setY(yy-graphSpacer-m_graphs[m_graph_index]->height()*m_scaleY);
                        redraw();
                    }
                    m_graph_index--;
                }
                if (!empty) break;

            }
        } else if (y > yy+graphSpacer+m_graphs[m_graph_index]->height()*m_scaleY) {
            // swapping downwards
            //qDebug() << "Graph Reorder" << m_graph_index;
            for (int i=m_graph_index+1;i<m_graphs.size();i++) {
                empty=m_graphs[i]->isEmpty() || (!m_graphs[i]->visible());
                p=m_graphs[m_graph_index];
                m_graphs[m_graph_index]=m_graphs[i];
                m_graphs[i]=p;
                if (!empty) {
                    m_sizer_point.setY(yy+graphSpacer+m_graphs[m_graph_index]->height()*m_scaleY);
                    redraw();
                }
                m_graph_index++;
                if (!empty) break;
            }
        }
        return;
    }

    float py = -m_offsetY;
    float h;

    for (int i=0; i < m_graphs.size(); i++) {

        if (m_graphs[i]->isEmpty() || (!m_graphs[i]->visible())) continue;

        h=m_graphs[i]->height() * m_scaleY;
        if (py > height())
            break; // we are done.. can't draw anymore

        if (!((y >= py+m_graphs[i]->top) && (y < py + h-m_graphs[i]->bottom))) {
            if (m_graphs[i]->isSelected()) {
                m_graphs[i]->deselect();
                timedRedraw(150);
                //redraw();
            }
        }
        if (m_button_down || ((py + h + graphSpacer) >= 0)) {
            if ((y >= py + h) && (y <= py + h + graphSpacer + 1)) {
                this->setCursor(Qt::SplitVCursor);
            } else if (!m_button_down && (y >= py) && (y < py+m_graphs[i]->top)) {
                // Mouse cursor is in top graph margin.
            } else if (!m_button_down && (y >= py+h-m_graphs[i]->bottom) && (y <= py+h)) {
                // Mouse cursor is in bottom grpah margin.
            } else if (m_button_down || ((y >= py+m_graphs[i]->top) && (y < py + h-m_graphs[i]->bottom))) {
                if (m_button_down || (x >= titleWidth+10)) { //(gYAxis::Margin-5)
                    this->setCursor(Qt::ArrowCursor);
                    m_horiz_travel+=qAbs(x-m_lastxpos)+qAbs(y-m_lastypos);
                    m_lastxpos=x;
                    m_lastypos=y;
                    QPoint p(x-titleWidth,y-py);
                    QMouseEvent e(event->type(),p,event->button(),event->buttons(),event->modifiers());

                    m_graphs[i]->mouseMoveEvent(&e);
                    if (!m_button_down && (x<=titleWidth+(gYAxis::Margin-5))) {
                        //qDebug() << "Hovering over" << m_graphs[i]->title();
                        if (m_graphsbytitle[STR_TR_EventFlags]==m_graphs[i]) {
                            QVector<Layer *> & layers=m_graphs[i]->layers();
                            gFlagsGroup *fg;
                            for (int i=0;i<layers.size();i++) {
                                if ((fg=dynamic_cast<gFlagsGroup *>(layers[i]))!=NULL) {
                                    float bh=fg->barHeight();
                                    int count=fg->count();
                                    float yp=py+m_graphs[i]->marginTop();
                                        yp=y-yp;
                                    float th=(float(count)*bh);
                                    if (yp>=0 && yp<th) {
                                        int i=yp/bh;
                                        if (i<count) {
                                            ChannelID code=fg->visibleLayers()[i]->code();
                                            QString ttip=schema::channel[code].description();
                                            m_tooltip->display(ttip,x,y-20,p_profile->general->tooltipTimeout());
                                            redraw();
                                            //qDebug() << code << ttip;
                                        }
                                    }

                                    break;
                                }
                            }
                        } else {
                            if (!m_graphs[i]->units().isEmpty()) {
                                m_tooltip->display(m_graphs[i]->units(),x,y-20,p_profile->general->tooltipTimeout());
                                redraw();
                            }
                        }
                    }
                } else {

                    this->setCursor(Qt::OpenHandCursor);
                }

            }

        }
        py+=h;
        py+=graphSpacer;
    }

}

void gGraphView::mousePressEvent(QMouseEvent * event)
{
    int x=event->x();
    int y=event->y();

    float py=-m_offsetY;
    float h;

    for (int i=0;i<m_graphs.size();i++) {

        if (m_graphs[i]->isEmpty() || (!m_graphs[i]->visible())) continue;

        h=m_graphs[i]->height()*m_scaleY;
        if (py>height())
            break;

        if ((py + h + graphSpacer) >= 0) {
            if ((y >= py) && (y < py + h)) {
                //qDebug() << "Clicked" << i;
                if (x < titleWidth+20) { // clicked on title to drag graph..
                    m_graph_dragging=true;
                    m_graph_index=i;
                    m_sizer_point.setX(x);
                    m_sizer_point.setY(py); // point at top of graph..
                    this->setCursor(Qt::ClosedHandCursor);
                } else { // send event to graph..
                    m_global_point_clicked=QPoint(x,y);
                    m_point_clicked=QPoint (x-titleWidth,y-py);

                    m_selected_graph=m_graphs[i];

                    QMouseEvent e(event->type(),m_point_clicked,event->button(),event->buttons(),event->modifiers());
                    m_graph_index=i;
                    m_button_down=true;
                    m_horiz_travel=0;
                    m_graphs[i]->mousePressEvent(&e);
                }
            } else if ((y >= py + h) && (y <= py + h + graphSpacer + 1)) {
                this->setCursor(Qt::SplitVCursor);
                m_sizer_dragging=true;
                m_sizer_index=i;
                m_sizer_point.setX(x);
                m_sizer_point.setY(y);
                //qDebug() << "Sizer clicked" << i;
            }

        }
        py+=h;
        py+=graphSpacer;

    }
}

void gGraphView::mouseReleaseEvent(QMouseEvent * event)
{
    this->setCursor(Qt::ArrowCursor);

    if (m_sizer_dragging) {
        m_sizer_dragging=false;
        return;
    }
    if (m_graph_dragging) {
        m_graph_dragging=false;
        return;
    }
    if (m_button_down) {
        m_button_down=false;
        int x1=m_global_point_clicked.x()-event->x();
        int y1=m_global_point_clicked.y()-event->y();

        QPoint p(m_point_clicked.x()-x1,m_point_clicked.y()-y1);
        QMouseEvent e(event->type(),p,event->button(),event->buttons(),event->modifiers());
        m_graphs[m_graph_index]->mouseReleaseEvent(&e);
    }
    //int x=event->x();
    //int y=event->y();
}

void gGraphView::mouseDoubleClickEvent(QMouseEvent * event)
{
    mousePressEvent(event);
    return;

/*    int x=event->x();
    int y=event->y();

    float py=-m_offsetY;
    float h;

    for (int i=0;i<m_graphs.size();i++) {

        if (m_graphs[i]->isEmpty()) continue;

        h=m_graphs[i]->height()*m_scaleY;
        if (py>height())
            break;

        if ((py + h + graphSpacer) >= 0) {
            if ((y >= py) && (y <= py + h)) {
                if (x < titleWidth) {
                    // What to do when double clicked on the graph title ??
                } else {
                    // send event to graph..
                    m_graphs[i]->mouseDoubleClickEvent(event);
                }
            } else if ((y >= py + h) && (y <= py + h + graphSpacer + 1)) {
                // What to do when double clicked on the resize handle?
            }

        }
        py+=h;
        py+=graphSpacer; // do we want the extra spacer down the bottom?
    } */
}
void gGraphView::wheelEvent(QWheelEvent * event)
{
    // Hmm.. I could optionalize this to change mousewheel behaviour without affecting the scrollbar now..

    if ((event->modifiers() & Qt::ControlModifier)) {
        int x=event->x();
        int y=event->y();

        float py=-m_offsetY;
        float h;

        for (int i=0;i<m_graphs.size();i++) {

            if (m_graphs[i]->isEmpty() || (!m_graphs[i]->visible())) continue;

            h=m_graphs[i]->height()*m_scaleY;
            if (py>height())
                break;

            if ((py + h + graphSpacer) >= 0) {
                if ((y >= py) && (y <= py + h)) {
                    if (x < titleWidth) {
                        // What to do when ctrl+wheel is used on the graph title ??
                    } else {
                        // send event to graph..
                        m_graphs[i]->wheelEvent(event);
                    }
                } else if ((y >= py + h) && (y <= py + h + graphSpacer + 1)) {
                    // What to do when the wheel is used on the resize handle?
                }

            }
            py+=h;
            py+=graphSpacer; // do we want the extra spacer down the bottom?
        }
    } else {
        m_scrollbar->SendWheelEvent(event); // Just forwarding the event to scrollbar for now..
        m_tooltip->cancel();
    }
}

void gGraphView::keyPressEvent(QKeyEvent * event)
{
    if (event->key()==Qt::Key_Tab) {
        event->ignore();
        return;
    }
    if (event->key()==Qt::Key_PageUp) {
            m_offsetY-=PROFILE.appearance->graphHeight()*3*m_scaleY;
            m_scrollbar->setValue(m_offsetY);
            m_offsetY=m_scrollbar->value();
            updateGL();
            return;
    } else if (event->key()==Qt::Key_PageDown) {
            m_offsetY+=PROFILE.appearance->graphHeight()*3*m_scaleY; //PROFILE.appearance->graphHeight();
            if (m_offsetY<0) m_offsetY=0;
            m_scrollbar->setValue(m_offsetY);
            m_offsetY=m_scrollbar->value();
            updateGL();
            return;
    //        redraw();
    }
    gGraph *g=NULL;
    int group=0;
    // Pick the first valid graph in the primary group
    for (int i=0;i<m_graphs.size();i++) {
        if (m_graphs[i]->group()==group) {
            if (!m_graphs[i]->isEmpty() && m_graphs[i]->visible()) {
                g=m_graphs[i];
                break;
            }
        }
    }

    if (!g) {
        for (int i=0;i<m_graphs.size();i++) {
            if (!m_graphs[i]->isEmpty()) {
                g=m_graphs[i];
                group=g->group();
                break;
            }
        }
    }
    if (!g) return;

    g->keyPressEvent(event);

    if (event->key()==Qt::Key_Left) {
        double xx=g->max_x-g->min_x;
        double zoom=8.0;
        if (event->modifiers() & Qt::ControlModifier) zoom/=4;

        g->min_x-=xx/zoom;;
        g->max_x=g->min_x+xx;
        if (g->min_x<g->rmin_x) {
            g->min_x=g->rmin_x;
            g->max_x=g->rmin_x+xx;
        }
        SetXBounds(g->min_x,g->max_x,group);
    } else if (event->key()==Qt::Key_Right) {
        double xx=g->max_x-g->min_x;
        double zoom=8.0;
        if (event->modifiers() & Qt::ControlModifier) zoom/=4;
        g->min_x+=xx/zoom;
        g->max_x=g->min_x+xx;
        if (g->max_x>g->rmax_x) {
            g->max_x=g->rmax_x;
            g->min_x=g->rmax_x-xx;
        }
        SetXBounds(g->min_x,g->max_x,group);
    } else if (event->key()==Qt::Key_Up) {
        float zoom=0.75;
        if (event->modifiers() & Qt::ControlModifier) zoom/=1.5;
        g->ZoomX(zoom,0); // zoom in.
    } else if (event->key()==Qt::Key_Down) {
        float zoom=1.33;
        if (event->modifiers() & Qt::ControlModifier) zoom*=1.5;
        g->ZoomX(zoom,0);  // Zoom out
    }
    //qDebug() << "Keypress??";
}
void gGraphView::setDay(Day * day)
{
    m_day=day;
    for (int i=0;i<m_graphs.size();i++) {
        m_graphs[i]->setDay(day);
    }
    ResetBounds(false);
}
bool gGraphView::isEmpty()
{
    bool res=true;
    for (int i=0;i<m_graphs.size();i++) {
        if (!m_graphs[i]->isEmpty()) {
            res=false;
            break;
        }
    }
    return res;
}

void gGraphView::refreshTimeout()
{
    redraw();
}
void gGraphView::timedRedraw(int ms)
{
    if (timer->isActive())
        timer->stop();
    timer->setSingleShot(true);
    timer->start(ms);
}
void gGraphView::resetLayout()
{
    int default_height=PROFILE.appearance->graphHeight();
    for (int i=0;i<m_graphs.size();i++) {
        m_graphs[i]->setHeight(default_height);
    }
    updateScale();
    redraw();
}
void gGraphView::deselect()
{
    for (int i=0;i<m_graphs.size();i++) {
        m_graphs[i]->deselect();
    }
}


MyScrollBar::MyScrollBar(QWidget * parent)
    :QScrollBar(parent)
{
}
void MyScrollBar::SendWheelEvent(QWheelEvent * e)
{
    wheelEvent(e);
}

const quint32 gvmagic=0x41756728;
const quint16 gvversion=0;

void gGraphView::SaveSettings(QString title)
{
    QString filename=PROFILE.Get("{DataFolder}")+QDir::separator()+title.toLower()+".shg";
    QFile f(filename);
    f.open(QFile::WriteOnly);
    QDataStream out(&f);
    out.setVersion(QDataStream::Qt_4_6);
    out.setByteOrder(QDataStream::LittleEndian);

    out << (quint32)gvmagic;
    out << (quint16)gvversion;

    out << (qint16)size();
    for (qint16 i=0;i<size();i++) {
        out << m_graphs[i]->title();
        out << m_graphs[i]->height();
        out << m_graphs[i]->visible();
        out << m_graphs[i]->RecMinY();
        out << m_graphs[i]->RecMaxY();
    }

    f.close();
}

bool gGraphView::LoadSettings(QString title)
{
    QString filename=PROFILE.Get("{DataFolder}")+QDir::separator()+title.toLower()+".shg";
    QFile f(filename);
    if (!f.exists()) return false;

    f.open(QFile::ReadOnly);
    QDataStream in(&f);
    in.setVersion(QDataStream::Qt_4_6);
    in.setByteOrder(QDataStream::LittleEndian);

    quint32 t1;
    quint16 t2;

    in >> t1;
    if (t1!=gvmagic) {
        qDebug() << "gGraphView" << title << "settings magic doesn't match" << t1 << gvmagic;
        return false;
    }
    in >> t2;
    if (t2!=gvversion) {
        qDebug() << "gGraphView" << title << "version doesn't match";
        return false;
    }

    qint16 siz;
    in >> siz;
    QString name;
    float hght;
    bool vis;
    EventDataType recminy,recmaxy;

    QVector<gGraph *> neworder;
    QHash<QString,gGraph *>::iterator gi;

    for (int i=0;i<siz;i++) {
        in >> name;
        in >> hght;
        in >> vis;
        in >> recminy;
        in >> recmaxy;
        gi=m_graphsbytitle.find(name);
        if (gi==m_graphsbytitle.end()) {
            qDebug() << "Graph" << name << "has been renamed or removed";
        } else {
            gGraph *g=gi.value();
            neworder.push_back(g);
            g->setHeight(hght);
            g->setVisible(vis);
            g->setRecMinY(recminy);
            g->setRecMaxY(recmaxy);
        }
    }

    if (neworder.size()==m_graphs.size()) {
        m_graphs=neworder;
    }

    f.close();
    updateScale();
    return true;
}

gGraph *gGraphView::findGraph(QString name)
{
    QHash<QString,gGraph*>::iterator i=m_graphsbytitle.find(name);
    if (i==m_graphsbytitle.end()) return NULL;
    return i.value();
}
int gGraphView::visibleGraphs()
{
    int cnt=0;
    for (int i=0;i<m_graphs.size();i++) {
        if (m_graphs[i]->visible() && !m_graphs[i]->isEmpty()) cnt++;
    }
    return cnt;
}
void gGraphView::redraw()
{
    if (!m_inAnimation)
        updateGL();
}
