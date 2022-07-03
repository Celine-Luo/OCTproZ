//This file is a modified version of code originally created by Martino Pilia, please see: https://github.com/m-pilia/volume-raycasting

/**
**  This file is part of OCTproZ.
**  OCTproZ is an open source software for processig of optical
**  coherence tomography (OCT) raw data.
**  Copyright (C) 2019-2022 Miroslav Zabic
**
**  OCTproZ is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program. If not, see http://www.gnu.org/licenses/.
**
****
** Author:	Miroslav Zabic
** Contact:	zabic
**			at
**			spectralcode.de
****
**/

/*
 * Copyright © 2018 Martino Pilia <martino.pilia@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>

#include <QtWidgets>

#include "glwindow3d.h"


/*!
 * \brief Convert a QColor to a QVector3D.
 * \return A QVector3D holding a RGB representation of the colour.
 */
QVector3D to_vector3d(const QColor& colour) {
	return QVector3D(colour.redF(), colour.greenF(), colour.blueF());
}


/*!
 * \brief Constructor for the canvas.
 * \param parent Parent widget.
 */
GLWindow3D::GLWindow3D(QWidget *parent)
	: QOpenGLWidget {parent}
	, raycastingVolume {nullptr}
{
	qRegisterMetaType<GLWindow3DParams >("GLWindow3DParams");
	this->panel = new ControlPanel3D(this);


	// Register available rendering modes here
	QStringList modes = { "Isosurface", "MIDA", "Alpha blending", "X-ray", "DMIP", "MIP"};
//	foreach(auto mode, modes){
//		m_modes[mode] = [&]() { GLWindow3D::raycasting(mode); };  //todo: figure out why this does not work
//	}
	m_modes["MIP"] = [&]() { GLWindow3D::raycasting("MIP"); };
	m_modes["DMIP"] = [&]() { GLWindow3D::raycasting("DMIP"); };
	m_modes["MIDA"] = [&]() { GLWindow3D::raycasting("MIDA"); };
	m_modes["Isosurface"] = [&]() { GLWindow3D::raycasting("Isosurface"); };
	m_modes["Alpha blending"] = [&]() { GLWindow3D::raycasting("Alpha blending"); };
	m_modes["X-ray"] = [&]() { GLWindow3D::raycasting("X-ray"); };


	this->panel->setModes(modes);

	this->delayedUpdatingRunning = false;
	this->initialized = false;
	this->changeTextureSizeFlag = false;
	this->updateContinuously = false;
	this->raycastingVolume = nullptr;

	//init panel
	this->setFocusPolicy(Qt::StrongFocus);
	this->layout = new QVBoxLayout(this);
	this->layout->addStretch();
	this->layout->addWidget(this->panel);
	this->panel->setVisible(false);

	this->initContextMenu();


	this->setMode("MIP");
	this->setThreshold(0.5);
	this->setStepLength(0.01f);

	this->fps = 0.0;
	//this->timer.start();
	this->counter = 0;

	this->viewPos.setX(0);
	this->viewPos.setY(0);

	connect(this->panel, &ControlPanel3D::displayParametersChanged, this, &GLWindow3D::slot_updateDisplayParams);
	connect(this->panel, &ControlPanel3D::lutSelected, this, &GLWindow3D::openLUTFromImage);
	connect(this->panel, &ControlPanel3D::info, this, &GLWindow3D::info);
	connect(this->panel, &ControlPanel3D::error, this, &GLWindow3D::error);
	connect(this->panel, &ControlPanel3D::dialogAboutToOpen, this, &GLWindow3D::dialogAboutToOpen);
	connect(this->panel, &ControlPanel3D::dialogClosed, this, &GLWindow3D::dialogClosed);
}


GLWindow3D::~GLWindow3D()
{
	this->saveSettings();
	foreach (auto element, m_shaders) {
		delete element.second;
	}
	delete this->raycastingVolume;
}

void GLWindow3D::setSettings(QVariantMap settings) {
	GLWindow3DParams params;
	params.extendedViewEnabled = settings.value(EXTENDED_PANEL).toBool();
	params.displayMode = settings.value(DISPLAY_MODE).toString();
	params.displayModeIndex = settings.value(DISPLAY_MODE_INDEX).toInt();
	params.threshold = settings.value(THRESHOLD).toReal();
	params.rayMarchStepLength = settings.value(RAY_STEP_LENGTH).toReal();
	params.stretchX = settings.value(STRETCH_X).toReal();
	params.stretchY= settings.value(STRETCH_Y).toReal();
	params.stretchZ = settings.value(STRETCH_Z).toReal();
	params.gamma = settings.value(GAMMA).toReal();
	params.depthWeight = settings.value(DEPTH_WEIGHT).toReal();
	params.smoothFactor = settings.value(SMOOTH_FACTOR).toInt();
	params.alphaExponent = settings.value(ALPHA_EXPONENT).toReal();
	params.shading = settings.value(SHADING_ENABLED).toBool();
	params.lutEnabled = settings.value(LUT_ENABLED).toBool();
	params.lutFileName = settings.value(LUT_FILENAME).toString();
	this->panel->setParams(params);
}

QVariantMap GLWindow3D::getSettings() {
	QVariantMap settings;
	GLWindow3DParams params = this->panel->getParams();
	settings.insert(EXTENDED_PANEL, params.extendedViewEnabled);
	settings.insert(DISPLAY_MODE, params.displayMode);
	settings.insert(DISPLAY_MODE_INDEX, params.displayModeIndex);
	settings.insert(THRESHOLD, params.threshold);
	settings.insert(RAY_STEP_LENGTH, params.rayMarchStepLength);
	settings.insert(STRETCH_X, params.stretchX);
	settings.insert(STRETCH_Y, params.stretchY);
	settings.insert(STRETCH_Z, params.stretchZ);
	settings.insert(GAMMA, params.gamma);
	settings.insert(DEPTH_WEIGHT, params.depthWeight);
	settings.insert(SMOOTH_FACTOR, params.smoothFactor);
	settings.insert(ALPHA_EXPONENT, params.alphaExponent);
	settings.insert(SHADING_ENABLED, params.shading);
	settings.insert(LUT_ENABLED, params.lutEnabled);
	settings.insert(LUT_FILENAME, params.lutFileName);
	return settings;
}

void GLWindow3D::setStretch(const qreal x, const qreal y, const qreal z) {
	if(this->raycastingVolume != nullptr){
		this->raycastingVolume->setStretch(x, y, z);
		update();
	}
}

void GLWindow3D::generateTestVolume() {
	if(this->raycastingVolume != nullptr){
		makeCurrent();
		this->raycastingVolume->generateTestVolume();
		doneCurrent();
	}
}

void GLWindow3D::initializeGL() {
	initializeOpenGLFunctions();

	if(this->raycastingVolume != nullptr){
		delete this->raycastingVolume;
		this->raycastingVolume = nullptr;
	}
	this->raycastingVolume = new RayCastVolume();
	this->raycastingVolume->createNoise();

	if(!this->initialized){
		this->addShader("Isosurface", ":/shaders/isosurface.vert", ":/shaders/isosurface.frag");
		this->addShader("Alpha blending", ":/shaders/alpha_blending.vert", ":/shaders/alpha_blending.frag");
		this->addShader("MIP", ":/shaders/maximum_intensity_projection.vert", ":/shaders/maximum_intensity_projection.frag");
		this->addShader("DMIP", ":/shaders/depth_mip.vert", ":/shaders/depth_mip.frag");
		this->addShader("MIDA", ":/shaders/mida.vert", ":/shaders/mida.frag");
		this->addShader("X-ray", ":/shaders/xray.vert", ":/shaders/xray.frag");
	}


	if(this->initialized){
		emit registerBufferCudaGL(this->raycastingVolume->getVolumeTexture()); //registerBufferCudaGL is necessary here because as soon as the openglwidget/dock is removed from the main window initializeGL() is called again. //todo: check if opengl context (buffer, texture,...) cleanup is necessary!
	}
	this->initialized = true;

	if(this->changeTextureSizeFlag){
		this->slot_changeBufferAndTextureSize(this->volumeWidth, this->volumeHeight, this->volumeDepth);
		this->changeTextureSizeFlag = false;
	}

	this->slot_updateDisplayParams(this->displayParams); //set display parameters that are restored from previous octproz session

	//restore lut settings from previous session
	QString fileName = this->displayParams.lutFileName;
	QImage lut(":/luts/hotter_lut.png"); //deafult lut
	if(fileName != ""){
		QImage lutFromFile(fileName);
		if(lutFromFile.isNull()){
			emit error(tr("Could not load LUT! File: ") + fileName);
			this->panel->eraseLUTFileName();
		}else{
			lut = lutFromFile;
		}
		//emit info(tr("LUT for volume rendering loaded! File used: ") + fileName);
	}
	makeCurrent();
	this->raycastingVolume->setLUT(lut);
	doneCurrent();
}



void GLWindow3D::resizeGL(int w, int h) {
	m_viewportSize = {(float) scaled_width(), (float) scaled_height()};
	m_aspectRatio = (float) scaled_width() / scaled_height();
	glViewport(0, 0, scaled_width(), scaled_height());
	//this->raycastingVolume->createNoise(); //todo: bugfix! software crashes sometimes here after createNoise() is called
}



void GLWindow3D::paintGL() {
	//this->countFPS();

	// Compute geometry
	m_viewMatrix.setToIdentity();
	m_viewMatrix.translate(this->viewPos.x(), this->viewPos.y(), -4.0f * std::exp(m_distExp / 600.0f));
	m_viewMatrix.rotate(m_trackBall.rotation());

	m_modelViewProjectionMatrix.setToIdentity();
	m_modelViewProjectionMatrix.perspective(m_fov, (float)scaled_width()/scaled_height(), 0.1f, 50.0f);
	m_modelViewProjectionMatrix *= m_viewMatrix * raycastingVolume->modelMatrix();

	m_normalMatrix = (m_viewMatrix * raycastingVolume->modelMatrix()).normalMatrix();

	m_rayOrigin = m_viewMatrix.inverted() * QVector3D({0.0f, 0.0f, 0.0f});

	// Perform raycasting
	m_modes[m_active_mode]();

	if(this->updateContinuously){
		update();
	}else{
		if(!this->delayedUpdatingRunning){
			this->delayedUpdatingRunning = true;
			QTimer::singleShot(DELAY_TIME_IN_ms, this, QOverload<>::of(&GLWindow3D::delayedUpdate)); //todo: consider using Gpu2HostNotifier to notify GLWindow3D when new volume data is available
		}
	}
}



GLuint GLWindow3D::scaled_width() {
	return devicePixelRatio() * width();
}


GLuint GLWindow3D::scaled_height() {
	return devicePixelRatio() * height();
}


void GLWindow3D::raycasting(const QString& shader) {
	m_shaders[shader]->bind();
	{
		m_shaders[shader]->setUniformValue("ViewMatrix", m_viewMatrix);
		m_shaders[shader]->setUniformValue("ModelViewProjectionMatrix", m_modelViewProjectionMatrix);
		m_shaders[shader]->setUniformValue("NormalMatrix", m_normalMatrix);
		m_shaders[shader]->setUniformValue("aspect_ratio", m_aspectRatio);
		m_shaders[shader]->setUniformValue("focal_length", m_focalLength);
		m_shaders[shader]->setUniformValue("viewport_size", m_viewportSize);
		m_shaders[shader]->setUniformValue("ray_origin", m_rayOrigin);
		m_shaders[shader]->setUniformValue("top", raycastingVolume->top());
		m_shaders[shader]->setUniformValue("bottom", raycastingVolume->bottom());
		m_shaders[shader]->setUniformValue("background_colour", to_vector3d(m_background));
		m_shaders[shader]->setUniformValue("light_position", m_lightPosition);
		m_shaders[shader]->setUniformValue("material_colour", m_diffuseMaterial);
		m_shaders[shader]->setUniformValue("step_length", m_stepLength);
		m_shaders[shader]->setUniformValue("threshold", m_threshold);
		m_shaders[shader]->setUniformValue("gamma", m_gamma);
		m_shaders[shader]->setUniformValue("volume", 0);
		m_shaders[shader]->setUniformValue("jitter", 1);
		m_shaders[shader]->setUniformValue("lut", 2);
		m_shaders[shader]->setUniformValue("depth_weight", m_depth_weight);
		m_shaders[shader]->setUniformValue("smooth_factor", m_smooth_factor);
		m_shaders[shader]->setUniformValue("alpha_exponent", m_alpha_exponent);
		m_shaders[shader]->setUniformValue("shading_enabled", m_shading_enabled);
		m_shaders[shader]->setUniformValue("lut_enabled", m_lut_enabled);

		glClearColor(m_background.redF(), m_background.greenF(), m_background.blueF(), m_background.alphaF());
		glClear(GL_COLOR_BUFFER_BIT);

		raycastingVolume->paint();
	}
	m_shaders[shader]->release();
}


QPointF GLWindow3D::pixelPosToViewPos(const QPointF& p) {
	return QPointF(2.0 * float(p.x()) / width() - 1.0, 1.0 - 2.0 * float(p.y()) / height());
}


void GLWindow3D::mouseDoubleClickEvent(QMouseEvent *event){
	if(!this->panel->underMouse()){
		this->m_distExp = -500;
		this->viewPos.setX(0);
		this->viewPos.setY(0);
		update();
	}
}



void GLWindow3D::enterEvent(QEvent *event){
	this->panel->setVisible(true);
}


void GLWindow3D::leaveEvent(QEvent *event){
	this->panel->setVisible(false);
}





/*!
 * \brief Callback for mouse movement.
 */
void GLWindow3D::mouseMoveEvent(QMouseEvent *event) {
	if (event->buttons() & Qt::LeftButton && !this->panel->underMouse()) {
		m_trackBall.move(pixelPosToViewPos(event->pos()), m_scene_trackBall.rotation().conjugated());
	}else if(event->buttons() &Qt::MiddleButton && !this->panel->underMouse()){
		QPoint delta = (event->pos() - this->mousePos);
		int windowWidth = this->size().width();
		int windowHeight = this->size().height();
		this->viewPos.rx() += 2.0*static_cast<float>(delta.x())/static_cast<float>(windowWidth);
		this->viewPos.ry() += -2.0*static_cast<float>(delta.y())/static_cast<float>(windowHeight);
	} else {
		m_trackBall.release(pixelPosToViewPos(event->pos()), m_scene_trackBall.rotation().conjugated());
	}
	this->mousePos = event->pos();
	update();
}


/*!
 * \brief Callback for mouse press.
 */
void GLWindow3D::mousePressEvent(QMouseEvent *event){
	this->mousePos = event->pos();
	if (event->buttons() & Qt::LeftButton && !this->panel->underMouse()) {
		m_trackBall.push(pixelPosToViewPos(event->pos()), m_scene_trackBall.rotation().conjugated());
	} else if (event->buttons() & Qt::MiddleButton) {

	}

	update();
}


/*!
 * \brief Callback for mouse release.
 */
void GLWindow3D::mouseReleaseEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton && !this->panel->underMouse()) {
		m_trackBall.release(pixelPosToViewPos(event->pos()), m_scene_trackBall.rotation().conjugated());
	}
	update();
}


/*!
 * \brief Callback for mouse wheel.
 */
void GLWindow3D::wheelEvent(QWheelEvent * event) {
	if(!this->panel->underMouse()){
		m_distExp += event->delta();
		if (m_distExp < -2800)
			m_distExp = -2800;
		if (m_distExp > 800)
			m_distExp = 800;
		update();
	}
}

void GLWindow3D::contextMenuEvent(QContextMenuEvent *event) {
	this->contextMenu->exec(event->globalPos());
}

void GLWindow3D::initContextMenu(){
	this->contextMenu = new QMenu(this);

	this->screenshotAction = new QAction(tr("&Screenshot..."), this);
	connect(this->screenshotAction, &QAction::triggered, this, &GLWindow3D::slot_screenshot);
	this->contextMenu->addAction(this->screenshotAction);
}

void GLWindow3D::slot_changeBufferAndTextureSize(unsigned int width, unsigned int height, unsigned int depth) {
	this->volumeWidth = width;
	this->volumeHeight = height;
	this->volumeDepth = depth;

	if(!this->initialized){
		this->changeTextureSizeFlag = true;
		return;
	}

	makeCurrent();
	this->raycastingVolume->changeBufferAndTextureSize(width, height, depth);
	doneCurrent();

	emit registerBufferCudaGL(this->raycastingVolume->getVolumeTexture());
}

void GLWindow3D::slot_initProcessingThreadOpenGL(QOpenGLContext *processingContext, QOffscreenSurface *processingSurface, QThread *processingThread) {
	QOpenGLContext* renderContext = this->context();
	(processingContext)->setFormat(renderContext->format());
	(processingContext)->setShareContext(renderContext);
	(processingContext)->create();
	(processingContext)->moveToThread(processingThread);
	(processingSurface)->setFormat(renderContext->format());
	(processingSurface)->create(); //Due to the fact that QOffscreenSurface is backed by a QWindow on some platforms, cross-platform applications must ensure that create() is only called on the main (GUI) thread
	(processingSurface)->moveToThread(processingThread);

	this->slot_changeBufferAndTextureSize(this->volumeWidth, this->volumeHeight, this->volumeDepth);
}

void GLWindow3D::slot_registerGLbufferWithCuda() {
	if(this->initialized){
		emit registerBufferCudaGL(this->raycastingVolume->getVolumeTexture());
	}
}

void GLWindow3D::slot_updateDisplayParams(GLWindow3DParams params) {
	this->displayParams = params;
	this->setMode(params.displayMode);
	this->setStepLength(params.rayMarchStepLength);
	this->setThreshold(params.threshold);
	this->setStretch(params.stretchX, params.stretchY, params.stretchZ);
	this->m_gamma = params.gamma;
	this->m_depth_weight = params.depthWeight;
	this->m_smooth_factor = params.smoothFactor;
	this->m_alpha_exponent = params.alphaExponent;
	this->m_shading_enabled = params.shading;
	this->m_lut_enabled = params.lutEnabled;
}

void GLWindow3D::openLUTFromImage(QImage lut){
	makeCurrent();
	this->raycastingVolume->setLUT(lut);
	doneCurrent();
	this->m_lut_enabled = true;
}

void GLWindow3D::delayedUpdate() {
	this->update();
	this->delayedUpdatingRunning = false;
}

void GLWindow3D::countFPS() {
	if(!this->timer.isValid()) {
		this->timer.start();
	}
	qreal elapsedTime = timer.elapsed();
	qreal captureInfoTime = 5000;
	this->counter++;
	if (elapsedTime >= captureInfoTime) {
		this->fps  = (qreal)counter / (elapsedTime / 1000.0);
		emit info(QString::number(fps));

	timer.restart();
	this->counter = 0;
	}
}

void GLWindow3D::saveSettings() {
	Settings::getInstance()->storeSettings(this->getName(), this->getSettings());
}

void GLWindow3D::slot_saveScreenshot(QString savePath, QString fileName) {
	QImage screenshot = this->grabFramebuffer();
	QString filePath = savePath + "/" + fileName;
	screenshot.save(filePath);
	emit info(tr("Screenshot saved to ") + filePath);
}

void GLWindow3D::slot_screenshot() {
	QImage screenshot = this->grabFramebuffer();
	emit dialogAboutToOpen();
	QCoreApplication::processEvents();
	QString filters("(*.png);;(*.jpg);;(*.bmp)");
	QString defaultFilter("(*.png)");
	QString fileName = QFileDialog::getSaveFileName(this, tr("Save screenshot to..."), QDir::currentPath(), filters, &defaultFilter);
	emit dialogClosed();
	if(fileName == ""){
		return;
	}
	bool saved = false;
	saved = screenshot.save(fileName);

	if(saved){
		emit info(tr("Screenshot saved to ") + fileName);
	}else{
		emit error(tr("Could not save screenshot to disk."));
	}
}

void GLWindow3D::addShader(const QString& name, const QString& vertex, const QString& fragment) {
	m_shaders[name] = new QOpenGLShaderProgram(this);
	m_shaders[name]->addShaderFromSourceFile(QOpenGLShader::Vertex, vertex);
	m_shaders[name]->addShaderFromSourceFile(QOpenGLShader::Fragment, fragment);
	m_shaders[name]->link();
}
