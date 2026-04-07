import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    property string currentScheme: schemeModel.get(typeCombo.currentIndex).scheme
    property alias url: urlField.text

    readonly property var schemeModel: ListModel {
        ListElement { text: "HTTPS";             scheme: "https";  placeholder: "https://download.opensuse.org/..." }
        ListElement { text: "HTTP";              scheme: "http";   placeholder: "http://server/path" }
        ListElement { text: "FTP";               scheme: "ftp";    placeholder: "ftp://server/path" }
        ListElement { text: "SMB/CIFS";          scheme: "smb";    placeholder: "smb://server/share/path" }
        ListElement { text: "NFS";               scheme: "nfs";    placeholder: "nfs://server/export/path" }
        ListElement { text: "CD/DVD";            scheme: "cd";     placeholder: "cd:///?devices=/dev/sr0" }
        ListElement { text: "ISO";     scheme: "iso";   placeholder: "iso:/?iso=/path/to/image.iso" }
        ListElement { text: qsTr("Local Directory"); scheme: "dir";    placeholder: "dir:///path/to/directory" }
    }

    spacing: 8

    ComboBox {
        id: typeCombo
        Layout.preferredWidth: 160
        model: root.schemeModel
        textRole: "text"
        currentIndex: 0

        onCurrentIndexChanged: {
            if (urlField.text === "" || urlField.text.match(/^\w+:\/\//)) {
                urlField.text = root.schemeModel.get(currentIndex).scheme + "://"
            }
        }
    }

    TextField {
        id: urlField
        Layout.fillWidth: true
        placeholderText: typeCombo.currentIndex >= 0
            ? root.schemeModel.get(typeCombo.currentIndex).placeholder : ""
        text: "https://"
        selectByMouse: true
    }

    function setUrl(newUrl) {
        urlField.text = newUrl
        // URLスキームに基づいてComboBoxを更新
        for (var i = 0; i < root.schemeModel.count; i++) {
            var s = root.schemeModel.get(i).scheme + "://"
            if (newUrl.startsWith(s)) {
                typeCombo.currentIndex = i
                return
            }
        }
    }

    function clear() {
        typeCombo.currentIndex = 0
        urlField.text = "https://"
    }

    function isValid() {
        return urlField.text.length > 8 && urlField.text.indexOf("://") > 0
    }
}
