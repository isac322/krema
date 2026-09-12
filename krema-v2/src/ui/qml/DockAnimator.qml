import QtQuick

/**
 * @brief Centralized animation primitive for consistent motion.
 * Uses KremaSettings for unified feel across compositors.
 */
QtObject {
    id: root

    // Animation configuration
    property int duration: kremaSettings.animationDuration
    property int easingType: kremaSettings.animationEasing

    /**
     * @brief Apply a standard animation to a target object/property.
     */
    function apply(target, property, toValue) {
        let anim = Qt.createQmlObject('import QtQuick; PropertyAnimation {}', target);
        anim.target = target;
        anim.property = property;
        anim.to = toValue;
        anim.duration = root.duration;
        anim.easing.type = root.easingType;
        anim.start();
        anim.onStopped.connect(() => anim.destroy());
    }
}
