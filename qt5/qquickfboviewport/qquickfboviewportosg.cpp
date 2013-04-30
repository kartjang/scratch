#include "qquickfboviewportosg.h"


QSGFBONode::QSGFBONode(QQuickWindow * window) :
    m_fbo(NULL),
    m_texture(NULL),
    m_window(window)
{
    connect(m_window,SIGNAL(beforeRendering()),
            this,SLOT(onRenderFBO()),
            Qt::DirectConnection);
}

QSGFBONode::~QSGFBONode()
{
    delete m_texture;
    delete m_fbo;
}

void QSGFBONode::onRenderFBO()
{
    QSize size = rect().size().toSize();

    // Create the FBO if it doesn't exist
    if(!m_fbo)   {
        this->initNode();
    }

    m_render_frame_mutex.lock();
    if(m_render_frame)   {
        m_render_frame = false;
        m_render_frame_mutex.unlock();

        // Bind rendering to the SG Node FBO
        m_fbo->bind();

        // Render the frame
        this->renderFrame();

        // Release rendering back to system FBO
        m_fbo->release();
    }
    else
    {   m_render_frame_mutex.unlock();   }
}

void QSGFBONode::onQueueFrame()
{
    m_render_frame_mutex.lock();
    m_render_frame = true;
    m_render_frame_mutex.unlock();
}

void QSGFBONode::renderFrame()
{
    m_osg_viewer->getCamera()->getGraphicsContext()->getState()->reset();
    m_osg_viewer->getCamera()->getGraphicsContext()->getState()->apply(m_osg_stateset);
    m_osg_viewer->frame();
    m_osg_viewer->getCamera()->getGraphicsContext()->getState()->captureCurrentState(*m_osg_stateset);

    // dunno about this one
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);

    // qt passes values to VertexAttrib 3,4,5 manually
    // so we should disable them when we use em
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);
    glDisableVertexAttribArray(5);

    // GL_DEPTH_TEST needs to be enabled when we render with
    // openscenegraph, but it must be disabled before passing
    // things back to QPainter
    glDisable(GL_DEPTH_TEST);
}

void QSGFBONode::initNode()
{
    QSize size = rect().size().toSize();
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    m_fbo = new QOpenGLFramebufferObject(size, format);
    m_texture = m_window->createTextureFromId(m_fbo->texture(), size);

    // init renderer here
    this->initOSG();

    this->setTexture(m_texture);
}

void QSGFBONode::initOSG()
{
        QSize size = rect().size().toSize();

    // draw a plane
    osg::ref_ptr<osg::Vec3Array> listVxArray = new osg::Vec3Array;
    listVxArray->push_back(osg::Vec3(-1,0,1));  // top left
    listVxArray->push_back(osg::Vec3(-1,0,-1)); // bottom left
    listVxArray->push_back(osg::Vec3(1,0,-1));  // bottom right
    listVxArray->push_back(osg::Vec3(1,0,1));   // top right

    osg::ref_ptr<osg::Vec2Array> listTxArray = new osg::Vec2Array;
    listTxArray->push_back(osg::Vec2(0,1));     // top left
    listTxArray->push_back(osg::Vec2(0,0));     // bottom left
    listTxArray->push_back(osg::Vec2(1,0));     // bottom right
    listTxArray->push_back(osg::Vec2(1,1));     // top right

    osg::ref_ptr<osg::DrawElementsUInt> listIdxs =
            new osg::DrawElementsUInt(GL_TRIANGLES,6);

    listIdxs->at(0) = 0;
    listIdxs->at(1) = 1;
    listIdxs->at(2) = 2;

    listIdxs->at(3) = 0;
    listIdxs->at(4) = 2;
    listIdxs->at(5) = 3;

    // load texture
    QString imgPath = "/home/preet/mrsnrub.jpg";
    osg::ref_ptr<osg::Image> texImg = osgDB::readImageFile(imgPath.toStdString());
    osg::ref_ptr<osg::Texture2D> texGeom = new osg::Texture2D;
    texGeom->setImage(texImg);
    texGeom->setFilter(osg::Texture2D::MIN_FILTER,osg::Texture2D::NEAREST);
    texGeom->setFilter(osg::Texture2D::MAG_FILTER,osg::Texture2D::NEAREST);
    texGeom->setWrap(osg::Texture2D::WRAP_S,osg::Texture2D::CLAMP_TO_EDGE);
    texGeom->setWrap(osg::Texture2D::WRAP_T,osg::Texture2D::CLAMP_TO_EDGE);

    // setup geometry
    osg::ref_ptr<osg::Geometry> geomMesh = new osg::Geometry;
    geomMesh->setVertexArray(listVxArray);
    geomMesh->setTexCoordArray(1,listTxArray);
    geomMesh->addPrimitiveSet(listIdxs);
    geomMesh->setUseVertexBufferObjects(true);
    geomMesh->getOrCreateStateSet()->setTextureAttributeAndModes(1,texGeom);

    // node mesh
    osg::ref_ptr<osg::Geode> geodeMesh = new osg::Geode;
    geodeMesh->addDrawable(geomMesh);

    // setup shaders
    QString m_shaderPrefix = "#version 120\n";
    QString m_resPrefix = "/home/preet/Dev/scratch/qt4/qdec_viewport_osg/";

    osg::ref_ptr<osg::Program> shProgram = new osg::Program;
    shProgram->setName("TextShader");

    QString vShader = readFileAsQString(m_resPrefix + "shaders/DirectTexture_vert.glsl");
    vShader.prepend(m_shaderPrefix);
    shProgram->addShader(new osg::Shader(osg::Shader::VERTEX,vShader.toStdString()));

    QString fShader = readFileAsQString(m_resPrefix + "shaders/DirectTexture_frag.glsl");
    vShader.prepend(m_shaderPrefix);
    shProgram->addShader(new osg::Shader(osg::Shader::FRAGMENT,fShader.toStdString()));

    osg::ref_ptr<osg::Uniform> myColor = new osg::Uniform("Color",osg::Vec4(0,1,1,1));
    osg::ref_ptr<osg::Uniform> myTexture = new osg::Uniform("Texture",1);

    // enable shader program
    osg::StateSet *ss = geodeMesh->getOrCreateStateSet();
    ss->addUniform(myColor);
    ss->addUniform(myTexture);
    ss->setAttributeAndModes(shProgram,osg::StateAttribute::ON);
    ss->setMode(GL_BLEND,osg::StateAttribute::ON);

    // add stuff to scene
    osg::ref_ptr<osg::Group> groupRoot = new osg::Group;
    groupRoot->addChild(geodeMesh);

    // create viewer and embedded window
    m_osg_viewer = new osgViewer::Viewer;
    m_osg_window = m_osg_viewer->setUpViewerAsEmbeddedInWindow(0,0,size.width(),size.height());

    // tell osg to insert uniforms and attributes in shaders
    m_osg_window->getState()->setUseModelViewAndProjectionUniforms(true);
    m_osg_window->getState()->setUseVertexAttributeAliasing(true);

    // config viewer
    m_osg_viewer->setCameraManipulator(new osgGA::TrackballManipulator);
    m_osg_viewer->setThreadingModel(osgViewer::Viewer::SingleThreaded);
    m_osg_viewer->setSceneData(groupRoot);
    m_osg_viewer->realize();

    // setup state
    m_osg_stateset = new osg::StateSet;

//    m_osg_viewer->getCamera()->getGraphicsContext()->getState()->setCheckForGLErrors(osg::State::ONCE_PER_ATTRIBUTE);

    // setup camera
//    m_osg_viewer->getCamera()->setClearColor(osg::Vec4(0,0,0,0));
//    m_osg_viewer->getCamera()->setClearMask(GL_DEPTH_BUFFER_BIT);
}

QString QSGFBONode::readFileAsQString(const QString &myFilePath)
{
    QFile myFile(myFilePath);
    myFile.open(QIODevice::ReadOnly);

    QTextStream textStream(&myFile);
    return textStream.readAll();
}



QQuickFBOViewport::QQuickFBOViewport()
{
    setFlag(ItemHasContents,true);

    QTimer * updTimer = new QTimer;
    connect(updTimer,SIGNAL(timeout()),
            this,SLOT(onRenderFrame()));
    updTimer->start(60);
}

void QQuickFBOViewport::onRenderFrame()
{
    emit queueFrame();
    this->update();
}

QSGNode * QQuickFBOViewport::updatePaintNode(QSGNode * oldNode,
                                             UpdatePaintNodeData * updData)
{
    QSGFBONode * fboNode = static_cast<QSGFBONode*>(oldNode);
    if(!fboNode)   {
        fboNode = new QSGFBONode(window());
        connect(this,SIGNAL(queueFrame()),
                fboNode,SLOT(onQueueFrame()));
    }
    fboNode->setRect(boundingRect());

    return fboNode;
}