# -*- coding: utf-8 -*-

#-------------------------------------------------------------------------------

# This file is part of Code_Saturne, a general-purpose CFD tool.
#
# Copyright (C) 1998-2019 EDF S.A.
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
# Street, Fifth Floor, Boston, MA 02110-1301, USA.

#-------------------------------------------------------------------------------

"""
This module contains the following class:
- InitializationView
"""

#-------------------------------------------------------------------------------
# Standard modules
#-------------------------------------------------------------------------------

import logging

#-------------------------------------------------------------------------------
# Third-party modules
#-------------------------------------------------------------------------------

from code_saturne.Base.QtCore    import *
from code_saturne.Base.QtGui     import *
from code_saturne.Base.QtWidgets import *

#-------------------------------------------------------------------------------
# Application modules import
#-------------------------------------------------------------------------------

from code_saturne.Pages.SourceTermsForm import Ui_SourceTermsForm

from code_saturne.Base.Toolbox import GuiParam
from code_saturne.Base.QtPage import IntValidator, DoubleValidator, ComboModel
from code_saturne.Pages.ThermalScalarModel import ThermalScalarModel
from code_saturne.Pages.DefineUserScalarsModel import DefineUserScalarsModel
from code_saturne.Pages.LocalizationModel import VolumicLocalizationModel, LocalizationModel
from code_saturne.Pages.SourceTermsModel import SourceTermsModel
from code_saturne.Pages.QMeiEditorView import QMeiEditorView
from code_saturne.Pages.OutputVolumicVariablesModel import OutputVolumicVariablesModel
from code_saturne.Pages.GroundwaterModel import GroundwaterModel
from code_saturne.Pages.NotebookModel import NotebookModel

#-------------------------------------------------------------------------------
# log config
#-------------------------------------------------------------------------------

logging.basicConfig()
log = logging.getLogger("InitializationView")
log.setLevel(GuiParam.DEBUG)

#-------------------------------------------------------------------------------
# Main class
#-------------------------------------------------------------------------------

class SourceTermsView(QWidget, Ui_SourceTermsForm):
    """
    """
    def __init__(self, parent, case, stbar):
        """
        Constructor
        """
        QWidget.__init__(self, parent)

        Ui_SourceTermsForm.__init__(self)
        self.setupUi(self)

        self.case = case
        self.case.undoStopGlobal()
        self.parent = parent

        self.mdl     = SourceTermsModel(self.case)
        self.notebook = NotebookModel(self.case)
        self.therm   = ThermalScalarModel(self.case)
        self.th_sca  = DefineUserScalarsModel(self.case)
        self.volzone = LocalizationModel('VolumicZone', self.case)

        # 0/ Read label names from XML file

        # Velocity

        # Thermal scalar
        namesca, unit = self.getThermalLabelAndUnit()
        self.th_sca_name = namesca

        # 1/ Combo box models

        # FIXME really useful to duplicate this comboBox for ground water flow?
        self.modelSpecies  = ComboModel(self.comboBoxSpecies, 1, 1)
        self.modelSpecies2 = ComboModel(self.comboBoxSpecies2, 1, 1)
        self.modelZone     = ComboModel(self.comboBoxZone, 1, 1)

        self.zone = ""
        zones = self.volzone.getZones()
        for zone in zones:
            active = 0
            if ('momentum_source_term' in zone.getNature().keys()):
                if (zone.getNature()['momentum_source_term'] == "on"):
                    active = 1

            if ('thermal_source_term' in zone.getNature().keys()):
                if (zone.getNature()['thermal_source_term']  == "on"):
                    active = 1

            if ('scalar_source_term' in zone.getNature().keys()):
                if (zone.getNature()['scalar_source_term']  == "on"):
                    active = 1

            if GroundwaterModel(self.case).getGroundwaterModel() != "off":
                self.groupBoxStandard.hide()
                # FIXME really useful to duplicate this groupBox for ground water flow?
                self.groupBoxTransport.show()
                if (zone.getNature()['momentum_source_term'] == "on"):
                    self.groupBoxRichards.show()
                    active = 1
                else:
                    self.groupBoxRichards.hide()
            else:
                self.groupBoxStandard.show()
                self.groupBoxRichards.hide()
                self.groupBoxTransport.hide()

            if (active):
                label = zone.getLabel()
                name = str(zone.getCodeNumber())
                self.modelZone.addItem(self.tr(label), name)
                if label == "all_cells":
                    self.zone = name
                if not self.zone:
                    self.zone = name
        self.modelZone.setItem(str_model = self.zone)

        scalar_list = self.th_sca.getUserScalarNameList()
        for s in self.th_sca.getScalarsVarianceList():
            if s in scalar_list: scalar_list.remove(s)
        if scalar_list != []:
            for scalar in scalar_list:
                self.scalar = scalar
                self.modelSpecies.addItem(self.tr(scalar),self.scalar)
                self.modelSpecies2.addItem(self.tr(scalar),self.scalar)
            self.modelSpecies.setItem(str_model = self.scalar)
            self.modelSpecies2.setItem(str_model = self.scalar)

        # 2/ Connections

        self.comboBoxZone.activated[str].connect(self.slotZone)
        self.comboBoxSpecies.activated[str].connect(self.slotSpeciesChoice)
        self.pushButtonMomentum.clicked.connect(self.slotMomentumFormula)
        self.pushButtonThermal.clicked.connect(self.slotThermalFormula)
        self.pushButtonSpecies.clicked.connect(self.slotSpeciesFormula)
        self.comboBoxSpecies2.activated[str].connect(self.slotSpeciesChoice)
        self.pushButtonSpecies2.clicked.connect(self.slotSpeciesGroundWaterFormula)
        self.pushButtonRichards.clicked.connect(self.slotRichardsFormula)

        # 3/ Initialize widget

        self.initialize(self.zone)

        self.case.undoStartGlobal()


    def initialize(self, zone_num):
        """
        Initialize widget when a new volumic zone is chosen
        """
        zone = self.case.xmlGetNode("zone", id=zone_num)

        if zone['momentum_source_term'] == "on":
            self.labelMomentum.show()
            self.pushButtonMomentum.show()
            exp = self.mdl.getMomentumFormula(self.zone)
            if exp:
                self.pushButtonMomentum.setToolTip(exp)
                self.pushButtonMomentum.setStyleSheet("background-color: green")
            else:
                self.pushButtonMomentum.setStyleSheet("background-color: red")

            if GroundwaterModel(self.case).getGroundwaterModel() != "off":
                self.groupBoxRichards.show()
                exp = self.mdl.getRichardsFormula(self.zone)
                if exp:
                    self.pushButtonRichards.setToolTip(exp)
                    self.pushButtonRichards.setStyleSheet("background-color: green")
                else:
                    self.pushButtonRichards.setStyleSheet("background-color: red")
            else:
                self.groupBoxRichards.hide()
        else:
            self.labelMomentum.hide()
            self.pushButtonMomentum.hide()
            self.groupBoxRichards.hide()

        if zone['thermal_source_term']  == "on":
            self.pushButtonThermal.show()
            self.labelThermal.show()
            if self.case['package'].name != "code_saturne" and self.th_sca_name=="":
                self.th_sca_name = 'enthalpy'
            exp = self.mdl.getThermalFormula(self.zone, self.th_sca_name)
            if exp:
                self.pushButtonThermal.setToolTip(exp)
                self.pushButtonThermal.setStyleSheet("background-color: green")
            else:
                self.pushButtonThermal.setStyleSheet("background-color: red")
        else:
            self.pushButtonThermal.hide()
            self.labelThermal.hide()

        if zone['scalar_source_term']  == "on":
            self.comboBoxSpecies.show()
            self.pushButtonSpecies.show()
            self.labelSpecies.show()

            exp = self.mdl.getSpeciesFormula(self.zone, self.scalar)
            if exp:
                self.pushButtonSpecies.setToolTip(exp)
                self.pushButtonSpecies.setStyleSheet("background-color: green")
            else:
                self.pushButtonSpecies.setStyleSheet("background-color: red")

            if GroundwaterModel(self.case).getGroundwaterModel() != "off":
                self.groupBoxTransport.show()

                exp = self.mdl.getGroundWaterSpeciesFormula(self.zone, self.scalar)
                if exp:
                    self.pushButtonSpecies2.setToolTip(exp)
                    self.pushButtonSpecies2.setStyleSheet("background-color: green")
                else:
                    self.pushButtonSpecies2.setStyleSheet("background-color: red")
            else:
                self.groupBoxTransport.hide()
        else:
            self.comboBoxSpecies.hide()
            self.pushButtonSpecies.hide()
            self.labelSpecies.hide()
            self.groupBoxTransport.hide()


    @pyqtSlot(str)
    def slotZone(self, text):
        """
        INPUT label for choice of zone
        """
        self.zone = self.modelZone.dicoV2M[str(text)]
        self.initialize(self.zone)


    @pyqtSlot(str)
    def slotSpeciesChoice(self, text):
        """
        INPUT label for choice of species
        """
        self.scalar = self.modelSpecies.dicoV2M[str(text)]
        exp = self.mdl.getSpeciesFormula(self.zone, self.scalar)
        if exp:
            self.pushButtonSpecies.setToolTip(exp)
            self.pushButtonSpecies.setStyleSheet("background-color: green")
        else:
            self.pushButtonSpecies.setToolTip("Use the formula editor to define a source term for this species.")
            self.pushButtonSpecies.setStyleSheet("background-color: red")


    @pyqtSlot()
    def slotMomentumFormula(self):
        """
        Set momentumFormula of the source term
        """
        exp = self.mdl.getMomentumFormula(self.zone)
        if not exp:
            exp = """Su = 0;\nSv = 0;\nSw = 0;\n
dSudu = 0;\ndSudv = 0;\ndSudw = 0;\n
dSvdu = 0;\ndSvdv = 0;\ndSvdw = 0;\n
dSwdu = 0;\ndSwdv = 0;\ndSwdw = 0;\n"""
        exa = """#example:\n
tau = 10.; # relaxation time (s)\n
vel_x_imp = 1.5; #target velocity (m/s)\n
Su = rho * (vel_x_imp - velocity[0]) / tau;\n
dSudu = - rho / tau; # Jacobian of the source term"""
        req = [('Su', "x component of the momentum source term"),
               ('Sv', "y component of the momentum source term"),
               ('Sw', "z component of the momentum source term"),
               ('dSudu', "x component x velocity derivative"),
               ('dSudv', "x component y velocity derivative"),
               ('dSudw', "x component z velocity derivative"),
               ('dSvdu', "y component x velocity derivative"),
               ('dSvdv', "y component y velocity derivative"),
               ('dSvdw', "y component z velocity derivative"),
               ('dSwdu', "z component x velocity derivative"),
               ('dSwdv', "z component y velocity derivative"),
               ('dSwdw', "z component z velocity derivative")]
        sym = [('x', 'cell center coordinate'),
               ('y', 'cell center coordinate'),
               ('z', 'cell center coordinate')]

        sym.append( ("velocity[0]", 'x velocity component'))
        sym.append( ("velocity[1]", 'y velocity component'))
        sym.append( ("velocity[2]", 'z velocity component'))
        sym.append( ("rho", 'local density (kg/m^3)'))

        for (nme, val) in self.notebook.getNotebookList():
            sym.append((nme, 'value (notebook) = ' + str(val)))

        dialog = QMeiEditorView(self,
                                check_syntax = self.case['package'].get_check_syntax(),
                                expression = exp,
                                required   = req,
                                symbols    = sym,
                                examples   = exa)
        if dialog.exec_():
            result = dialog.get_result()
            log.debug("slotFormulaVelocity -> %s" % str(result))
            self.mdl.setMomentumFormula(self.zone, str(result))
            self.pushButtonMomentum.setToolTip(result)
            self.pushButtonMomentum.setStyleSheet("background-color: green")


    @pyqtSlot()
    def slotSpeciesFormula(self):
        """
        """
        exp = self.mdl.getSpeciesFormula(self.zone, self.scalar)
        if not exp:
            exp = """S = 0;\ndS = 0;\n"""
        exa = """#example: """
        req = [('S', 'species source term'),
               ('dS', 'species source term derivative')]
        sym = [('x', 'cell center coordinate'),
               ('y', 'cell center coordinate'),
               ('z', 'cell center coordinate')]

        name = self.th_sca.getScalarName(self.scalar)
        sym.append((name, 'current species'))

        for (nme, val) in self.notebook.getNotebookList():
            sym.append((nme, 'value (notebook) = ' + str(val)))

        dialog = QMeiEditorView(self,
                                check_syntax = self.case['package'].get_check_syntax(),
                                expression = exp,
                                required   = req,
                                symbols    = sym,
                                examples   = exa)
        if dialog.exec_():
            result = dialog.get_result()
            log.debug("slotFormulaSpecies -> %s" % str(result))
            self.mdl.setSpeciesFormula(self.zone, self.scalar, str(result))
            self.pushButtonSpecies.setToolTip(result)
            self.pushButtonSpecies.setStyleSheet("background-color: green")


    @pyqtSlot()
    def slotSpeciesGroundWaterFormula(self):
        """
        """
        exp = self.mdl.getGroundWaterSpeciesFormula(self.zone, self.scalar)
        if not exp:
            exp = """Q = 0;"""
        exa = """#example: """
        req = [('Q', 'species source term')]
        sym = [('x', 'cell center coordinate'),
               ('y', 'cell center coordinate'),
               ('z', 'cell center coordinate'),
               ('t', 'current time')]

        name = self.th_sca.getScalarName(self.scalar)
        sym.append((name, 'current species'))

        for (nme, val) in self.notebook.getNotebookList():
            sym.append((nme, 'value (notebook) = ' + str(val)))

        dialog = QMeiEditorView(self,
                                check_syntax = self.case['package'].get_check_syntax(),
                                expression = exp,
                                required   = req,
                                symbols    = sym,
                                examples   = exa)
        if dialog.exec_():
            result = dialog.get_result()
            log.debug("slotSpeciesGroundWaterFormula -> %s" % str(result))
            self.mdl.setGroundWaterSpeciesFormula(self.zone, self.scalar, str(result))
            self.pushButtonSpecies2.setToolTip(result)
            self.pushButtonSpecies2.setStyleSheet("background-color: green")


    @pyqtSlot()
    def slotRichardsFormula(self):
        """
        """
        exp = self.mdl.getRichardsFormula(self.zone)
        if not exp:
            exp = """Qs = 0;\n"""
        exa = """#example: """
        req = [('Qs', 'volumetric source term')]
        sym = [('x', 'cell center coordinate'),
               ('y', 'cell center coordinate'),
               ('z', 'cell center coordinate'),
               ('t', 'current time')]

        for (nme, val) in self.notebook.getNotebookList():
            sym.append((nme, 'value (notebook) = ' + str(val)))

        dialog = QMeiEditorView(self,
                                check_syntax = self.case['package'].get_check_syntax(),
                                expression = exp,
                                required   = req,
                                symbols    = sym,
                                examples   = exa)
        if dialog.exec_():
            result = dialog.get_result()
            log.debug("slotRichardsFormula -> %s" % str(result))
            self.mdl.setRichardsFormula(self.zone, str(result))
            self.pushButtonRichards.setToolTip(result)
            self.pushButtonRichards.setStyleSheet("background-color: green")


    def getThermalLabelAndUnit(self):
        """
        Define the type of model is used.
        """
        model = self.therm.getThermalScalarModel()

        if model != 'off':
            th_sca_name = self.therm.getThermalScalarName()
            if model == "temperature_celsius":
                unit = "<sup>o</sup>C"
            elif model == "temperature_kelvin":
                unit = "Kelvin"
            elif model == "enthalpy":
                unit = "J/kg"
        else:
            th_sca_name = ''
            unit = None

        self.th_sca_name = th_sca_name

        return th_sca_name, unit


    @pyqtSlot()
    def slotThermalFormula(self):
        """
        Input the initial formula of thermal scalar
        """
        exp = self.mdl.getThermalFormula(self.zone, self.th_sca_name)
        if not exp:
            exp = self.mdl.getDefaultThermalFormula(self.th_sca_name)
        exa = """#example: """
        req = [('S', 'thermal source term'),
               ('dS', 'thermal source term derivative')]
        sym = [('x', 'cell center coordinate'),
               ('y', 'cell center coordinate'),
               ('z', 'cell center coordinate')]

        if self.case['package'].name == 'code_saturne':
            if self.therm.getThermalScalarModel() == 'enthalpy':
                sym.append(('enthalpy', 'thermal scalar'))
            if self.therm.getThermalScalarModel() == 'total_energy':
                sym.append(('total_energy', 'thermal scalar'))
            else:
                sym.append(('temperature', 'thermal scalar'))
        else:
            sym.append(('enthalpy', 'Enthalpy'))

        for (nme, val) in self.notebook.getNotebookList():
            sym.append((nme, 'value (notebook) = ' + str(val)))

        dialog = QMeiEditorView(self,
                                check_syntax = self.case['package'].get_check_syntax(),
                                expression = exp,
                                required   = req,
                                symbols    = sym,
                                examples   = exa)
        if dialog.exec_():
            result = dialog.get_result()
            log.debug("slotFormulaThermal -> %s" % str(result))
            self.mdl.setThermalFormula(self.zone, self.th_sca_name, str(result))
            self.pushButtonThermal.setToolTip(result)
            self.pushButtonThermal.setStyleSheet("background-color: green")


    def tr(self, text):
        """
        Translation
        """
        return text


#-------------------------------------------------------------------------------
# Testing part
#-------------------------------------------------------------------------------


if __name__ == "__main__":
    pass


#-------------------------------------------------------------------------------
# End
#-------------------------------------------------------------------------------
