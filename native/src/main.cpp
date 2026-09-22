// SPDX-License-Identifier: MIT
#include "controller.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("ECO");
    app.setApplicationName("eco-workstation");
    QQuickStyle::setStyle("Basic");
    Controller controller;
    QQmlApplicationEngine qml;
    qml.rootContext()->setContextProperty("workstation", &controller);
    qml.load(QUrl("qrc:/Main.qml"));
    if (qml.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
