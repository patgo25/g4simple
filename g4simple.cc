#include <iostream>
#include <vector>
#include <string>
#include <regex>
#include <utility>

#include "G4RunManager.hh"
#include "G4Run.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"
#include "G4GeneralParticleSource.hh"
#include "G4UIterminal.hh"
#include "G4UItcsh.hh"
#include "G4UImanager.hh"
#include "G4PhysListFactory.hh"
#include "G4VisExecutive.hh"
#include "G4UserSteppingAction.hh"
#include "G4Track.hh"
#include "G4EventManager.hh"
#include "G4UIdirectory.hh"
#include "G4UIcmdWithAString.hh"
#include "G4UIcmdWithABool.hh"
#include "G4UIcmdWithADouble.hh"
#include "G4UIcmdWithAnInteger.hh"
#include "G4GDMLParser.hh"
#include "G4TouchableHandle.hh"
#include "G4PhysicalVolumeStore.hh"
#include "G4tgbVolumeMgr.hh"
#include "G4tgrMessenger.hh"
#include "Randomize.hh"

#include "G4OpticalPhysics.hh"
#include "OpNoviceDetectorConstruction.hh"
#include "L200DetectorConstruction.hh"
//#include "L200OpBoundaryProcess.hh"
#include "MapRunAction.hh"

#include "L200ParticleGenerator.hh"
#include "L200FiberPhysics.hh"
#include "L200OpBoundaryProcess.hh"
#include "RunList.hh"

#include "g4root.hh"
#include "g4xml.hh"
#include "g4csv.hh"
#ifdef GEANT4_USE_HDF5
#include "g4hdf5.hh"
#endif

using namespace std;
using namespace CLHEP;

const G4double lambda = twopi*1.973269602e-16 * m * GeV;
G4double fiberDetProb = 0.;

class G4SimpleSteppingAction : public G4UserSteppingAction, public G4UImessenger
{
  protected:
    G4UIcommand* fVolIDCmd;
    G4UIcmdWithAString* fOutputFormatCmd;
    G4UIcmdWithAString* fOutputOptionCmd;
    G4UIcmdWithABool* fRecordAllStepsCmd;
    G4UIcmdWithADouble* fSetFiberAbsProbCmd;
	  G4UIcmdWithAnInteger* fSetVerboseCmd;

    enum EFormat { kCsv, kXml, kRoot, kHdf5 };
    EFormat fFormat;
    enum EOption { kStepWise, kEventWise };
    EOption fOption;
    bool fRecordAllSteps;
	int verbosity;

    vector< pair<string,string> > fPatternPairs;	//have to throw out regex due to ancient gcc 4.8.x not supporting it

    G4int fNEvents;
    G4int fEventNumber;
    vector<G4int> fPID;
    vector<G4int> fTrackID;
    vector<G4int> fParentID;
    vector<G4int> fStepNumber;
    vector<G4double> fKE;
    vector<G4double> fEDep;
    vector<G4double> fX;
    vector<G4double> fY;
    vector<G4double> fZ;
    vector<G4double> fLX;
    vector<G4double> fLY;
    vector<G4double> fLZ;
    vector<G4double> fPdX;
    vector<G4double> fPdY;
    vector<G4double> fPdZ;
    vector<G4double> fT;
    vector<G4int> fVolID;
    vector<G4int> fIRep;


    G4double fiberAbsProb;		//fiber absorption
    map<G4VPhysicalVolume*, int> fVolIDMap;

	MapRunAction* mra;

  public:
    G4SimpleSteppingAction(MapRunAction* mra) : fNEvents(0), fEventNumber(0), mra(mra), verbosity(4) {
      ResetVars();

      fVolIDCmd = new G4UIcommand("/g4simple/setVolID", this);
      fVolIDCmd->SetParameter(new G4UIparameter("pattern", 's', false));
      fVolIDCmd->SetParameter(new G4UIparameter("replacement", 's', false));
      fVolIDCmd->SetGuidance("Volumes with name matching [pattern] will be given volume ID "
                             "based on the [replacement] rule. Replacement rule must produce an integer."
                             " Patterns which replace to 0 or -1 are forbidden and will be omitted.");

      fOutputFormatCmd = new G4UIcmdWithAString("/g4simple/setOutputFormat", this);
      string candidates = "csv xml root";
#ifdef GEANT4_USE_HDF5
      candidates += " hdf5";
#endif
      fOutputFormatCmd->SetCandidates(candidates.c_str());
      fOutputFormatCmd->SetGuidance("Set output format");
      fFormat = kCsv;

      fOutputOptionCmd = new G4UIcmdWithAString("/g4simple/setOutputOption", this);
      candidates = "stepwise eventwise";
      fOutputOptionCmd->SetCandidates(candidates.c_str());
      fOutputOptionCmd->SetGuidance("Set output option:");
      fOutputOptionCmd->SetGuidance("  stepwise: one row per step");
      fOutputOptionCmd->SetGuidance("  eventwise: one row per event");
      fOption = kStepWise;

      fSetFiberAbsProbCmd = new G4UIcmdWithADouble("/optics/fiberAbsProb", this);
      fSetFiberAbsProbCmd->SetDefaultValue(0.1);
      fSetFiberAbsProbCmd->SetGuidance("Set the detection probability of the fiber shrouds (absorption)!");
      fiberAbsProb = 0.;


      fRecordAllStepsCmd = new G4UIcmdWithABool("/g4simple/recordAllSteps", this);
      fRecordAllStepsCmd->SetParameterName("recordAllSteps", true);
      fRecordAllStepsCmd->SetDefaultValue(true);
      fRecordAllStepsCmd->SetGuidance("Write out every single step, not just those in sensitive volumes.");
      fRecordAllSteps = false;

	  fSetVerboseCmd = new G4UIcmdWithAnInteger("/g4simple/verbose", this);
	  fSetVerboseCmd->SetGuidance("Sets verbosity of stepping.");
    }

    G4VAnalysisManager* GetAnalysisManager() {
      if(fFormat == kCsv) return G4Csv::G4AnalysisManager::Instance();
      if(fFormat == kXml) return G4Xml::G4AnalysisManager::Instance();
      if(fFormat == kRoot) return G4Root::G4AnalysisManager::Instance();
      if(fFormat == kHdf5) {
#ifdef GEANT4_USE_HDF5
        return G4Hdf5::G4AnalysisManager::Instance();
#else
        cout << "Warning: You need to compile Geant4 with cmake flag "
             << "-DGEANT4_USE_HDF5 in order to generate the HDF5 output format.  "
             << "Reverting to ROOT." << endl;
        return G4Root::G4AnalysisManager::Instance();
#endif
      }
      cout << "Error: invalid format " << fFormat << endl;
      return NULL;
    }

    ~G4SimpleSteppingAction() {
      G4VAnalysisManager* man = GetAnalysisManager();
      if(man->IsOpenFile()) {
        if(fOption == kEventWise && fPID.size()>0) WriteRow(man);
        man->Write();
        man->CloseFile();
      }
      delete man;
      delete fVolIDCmd;
      delete fOutputFormatCmd;
      delete fOutputOptionCmd;
      delete fRecordAllStepsCmd;
      delete fSetFiberAbsProbCmd;
    }

    void SetNewValue(G4UIcommand *command, G4String newValues) {
      if(command == fVolIDCmd) {
        istringstream iss(newValues);
        string pattern;
        string replacement;
        iss >> pattern >> replacement;
        fPatternPairs.push_back(pair<string,string>(pattern,replacement));
      }
      if(command == fOutputFormatCmd) {
        // also set recommended options.
        // override option by subsequent call to /g4simple/setOutputOption
        if(newValues == "csv") {
          fFormat = kCsv;
          fOption = kStepWise;
        }
        if(newValues == "xml") {
          fFormat = kXml;
          fOption = kEventWise;
        }
        if(newValues == "root") {
          fFormat = kRoot;
          fOption = kEventWise;
        }
        if(newValues == "hdf5") {
          fFormat = kHdf5;
          fOption = kStepWise;
        }
        GetAnalysisManager(); // call once to make all of the /analysis commands available
      }
      if(command == fOutputOptionCmd) {
        if(newValues == "stepwise") fOption = kStepWise;
        if(newValues == "eventwise") fOption = kEventWise;
      }
      if(command == fRecordAllStepsCmd) {
        fRecordAllSteps = fRecordAllStepsCmd->GetNewBoolValue(newValues);
      }
     if(command == fSetFiberAbsProbCmd){
	fiberAbsProb = fSetFiberAbsProbCmd->GetNewDoubleValue(newValues);
      }
	  if(command == fSetVerboseCmd){
		verbosity = fSetVerboseCmd->GetNewIntValue(newValues);
	  }
    }

    void ResetVars() {
      fPID.clear();
      fTrackID.clear();
      fParentID.clear();
      fStepNumber.clear();
      fKE.clear();
      fEDep.clear();
      fX.clear();
      fY.clear();
      fZ.clear();
      fLX.clear();
      fLY.clear();
      fLZ.clear();
      fPdX.clear();
      fPdY.clear();
      fPdZ.clear();
      fT.clear();
      fVolID.clear();
      fIRep.clear();
    }

    void WriteRow(G4VAnalysisManager* man) {
      man->FillNtupleIColumn(0, fNEvents);
      man->FillNtupleIColumn(1, fEventNumber);
      int row = 2;
      if(fOption == kStepWise) {
        size_t i = fPID.size()-1;
        man->FillNtupleIColumn(row++, fPID[i]);
        man->FillNtupleIColumn(row++, fTrackID[i]);
        man->FillNtupleIColumn(row++, fParentID[i]);
        man->FillNtupleIColumn(row++, fStepNumber[i]);
        man->FillNtupleDColumn(row++, fKE[i]);
        man->FillNtupleDColumn(row++, fEDep[i]);
        man->FillNtupleDColumn(row++, fX[i]);
        man->FillNtupleDColumn(row++, fY[i]);
        man->FillNtupleDColumn(row++, fZ[i]);
        man->FillNtupleDColumn(row++, fLX[i]);
        man->FillNtupleDColumn(row++, fLY[i]);
        man->FillNtupleDColumn(row++, fLZ[i]);
        man->FillNtupleDColumn(row++, fPdX[i]);
        man->FillNtupleDColumn(row++, fPdY[i]);
        man->FillNtupleDColumn(row++, fPdZ[i]);
        man->FillNtupleDColumn(row++, fT[i]);
        man->FillNtupleIColumn(row++, fVolID[i]);
        man->FillNtupleIColumn(row++, fIRep[i]);
      }
      // for event-wise, manager copies data from vectors over
      // automatically in the next line
      man->AddNtupleRow();
    }

    void UserSteppingAction(const G4Step *step) {
		//NOW before everything else as we need volID in optical detection
	  // post-step point will always work: only need to use the pre-step point
      // on the first step, for which the pre-step volume is always the same as
      // the post-step volume
      G4VPhysicalVolume* vpv = step->GetPostStepPoint()->GetPhysicalVolume();
      G4int id = fVolIDMap[vpv];
      if(id == 0 && fPatternPairs.size() > 0) {
        string name = (vpv == NULL) ? "NULL" : vpv->GetName();
        for(auto& pp : fPatternPairs) {
          if(name == pp.first) {
            string replaced = pp.second;
	    cout << "Setting ID for " << name << " to " << replaced << endl;
            int id_new = stoi(replaced);
            if (id_new == 0 || id_new == -1) {
              cout << "Volume " << name << ": Can't use ID = " << id_new << endl;
            }
            else {
              id = id_new;
            }
            break;
          }
        }
        if(id == 0 && !fRecordAllSteps) id = -1;
        fVolIDMap[vpv] = id;
      }

        //int verbosity = 4;		//wir haben dafür jetzt nen Command!

        //int verbosity = 2;

      G4VAnalysisManager* man = GetAnalysisManager();


		/* ----------------- TEST */
	if(step->GetPostStepPoint()->GetPhysicalVolume()==NULL){
		if(verbosity>2){G4cout << "    Oh. @ End of World..." << G4endl;}

	}else{		//do this to prevent crash @ end of world


		G4String actualVolume = step->GetPostStepPoint()->GetPhysicalVolume()->GetName();
		G4String preVolume = step->GetPreStepPoint()->GetPhysicalVolume()->GetName();

    //Suche den L200OpBoundaryProcess:
    L200OpBoundaryProcess* boundary_proc=NULL;
    G4ProcessManager* proc_man = step->GetTrack()->GetDefinition()->GetProcessManager();
    int proc_num = proc_man->GetProcessListLength();
    G4ProcessVector* proc_vec = proc_man->GetProcessList();
    for(int i = 0; i < proc_num; i++){
        if((*proc_vec)[i]->GetProcessName()=="OpBoundary"){
            boundary_proc = (L200OpBoundaryProcess*)(*proc_vec)[i];
            break;
        }
    }
    if(boundary_proc &&
    step->GetTrack()->GetDefinition() == G4OpticalPhoton::OpticalPhotonDefinition()){
        L200OpBoundaryProcessStatus boundaryStatus=boundary_proc->GetStatus();
	G4double p = G4UniformRand();
	switch(boundaryStatus){
        case Absorption:
            /*Do Nothing... */
			if(verbosity>3){G4cout << "Photon absorbed @ boundary of "<<actualVolume << G4endl;}
            break;
        case Detection:{
			//old way of adding per hand; should no longer be needed by now
			//G4SDManager* localSDman = G4SDManager::GetSDMpointer();
            //PMTConstruction::notifyPMTSD(step, localSDman);
			if(verbosity>3){G4cout << "Photon detected @ boundary of "<<actualVolume << G4endl;}
			//mra->increment(fVolIDMap[step->GetPostStepPoint()->GetPhysicalVolume()]);
			}
			break;
		case FresnelReflection:
            if(verbosity>3)G4cout << "FresnelReflection" << G4endl;
            break;
	case FresnelRefraction:
            if(verbosity>3){
		G4cout << "FresnelRefraction"<<G4endl;
		}
		//Lets check if the photon is blue. Otherwise Prob is handled by TPB magic.
		if((lambda/step->GetTrack()->GetKineticEnergy()) > 400*nm){
	    	   if(actualVolume == "innerShroud" || actualVolume == "outerShroud"){
	    		if(preVolume == "larVolume"){
					//See if the photon gets into the fiber and absorbed
					if(p <= fiberAtt(step)*fiberDetProb){
						if(verbosity>3){G4cout << "Yeees photon absorbed with a probabiltity of " << p << " < " << fiberAtt(step) << G4endl;}
						mra->increment(fVolIDMap[step->GetPostStepPoint()->GetPhysicalVolume()]);
						step->GetTrack()->SetTrackStatus(fStopAndKill);
					}
					//Ok so it hit the fiber and didn't get absobed -> Kill it
					else if(p<= fiberDetProb){
						step->GetTrack()->SetTrackStatus(fStopAndKill);
						if(verbosity>3){G4cout << "Hit the shroud but was not absorbed -> KILL"<< G4endl;}
					}
					//Photon didn't hit a fiber -> let it go on

		  	}
		    }
		}
		else{
		if(verbosity>3){G4cout << "Photon has WL of " << lambda/step->GetTrack()->GetKineticEnergy()/nm << " nm which is not blue -> Ignore "<< G4endl;}
		}
            break;
        case TotalInternalReflection:
            if(verbosity>3)G4cout << "TotalInternalReflection" << G4endl;
            break;
        case LambertianReflection:
            if(verbosity>3)G4cout << "LambertianReflection" << G4endl;
            break;
        case LobeReflection:
            if(verbosity>3)G4cout << "LobeReflection" << G4endl;
            break;
        case SpikeReflection:
            if(verbosity>3)G4cout << "SpikeReflection" << G4endl;
            break;
        case BackScattering:
            if(verbosity>3)G4cout << "BackScattering" << G4endl;
            break;
        case StepTooSmall:
            if(verbosity>3)G4cout << "The step is too small." << G4endl;
            break;
        case NoRINDEX:
            if(verbosity>-1){G4cout<<"WARNING: missing refractive Index for boundary "
								<<actualVolume<< G4endl;}
            break;
		case Undefined:
            if(verbosity>3)G4cout << "The step is undefined." << G4endl;
            break;
		case NotAtBoundary:
            if(verbosity>3)G4cout << "NotAtBoundary" << G4endl;
            break;
	case SameMaterial:
            if(verbosity>3){G4cout << "Flying from " << preVolume << " to " << actualVolume  << G4endl;}
	    break;
	case TPBMagic:
		if(verbosity>3){
			G4cout <<"Doing TPB magic at " << preVolume << " to " << actualVolume  << G4endl;
			G4cout <<"Changing momentum dir from ";
			G4cout << step->GetPreStepPoint()->GetMomentumDirection() << " to ";
			G4cout << step->GetPostStepPoint()->GetMomentumDirection() << G4endl;


			G4cout << "PreVoliume: " << step->GetPreStepPoint()->GetPhysicalVolume()->GetName() << G4endl;
			G4cout << "PostVolium: " << step->GetPostStepPoint()->GetPhysicalVolume()->GetName() << G4endl;
		}


		//Here only roll for absorbtion since we already rolled in Boundary class for detection
		 if(step->GetPostStepPoint()->GetPhysicalVolume() != step->GetPreStepPoint()->GetPhysicalVolume()){
		//if(p <= fiberAbsProb){
		if(p <= fiberAtt(step)){
				mra->increment(fVolIDMap[step->GetPostStepPoint()->GetPhysicalVolume()]);
								if(verbosity>3){G4cout << "Yeees 128 nm photon absorbed with a probabiltity of " << fiberAtt(step) << G4endl;}

		}
		step->GetTrack()->SetTrackStatus(fStopAndKill);
}
	    break;
        default:
		if(verbosity>3)G4cout << "Unknown Photon-boundary-Action @ "<<actualVolume <<": "<<boundaryStatus<< G4endl;
            break;
      }
    }}

		return;		//TODO dirty trick to shut off normal writing to not interfere with custom

		/*  TEST --------------------- */

      if(!man->IsOpenFile()) {
        // need to create the ntuple before opening the file in order to avoid
        // writing error in csv, xml, and hdf5
        man->CreateNtuple("g4sntuple", "steps data");
        man->CreateNtupleIColumn("nEvents");
        man->CreateNtupleIColumn("event");
        if(fOption == kEventWise) {
          man->CreateNtupleIColumn("pid", fPID);
          man->CreateNtupleIColumn("trackID", fTrackID);
          man->CreateNtupleIColumn("parentID", fParentID);
          man->CreateNtupleIColumn("step", fStepNumber);
          man->CreateNtupleDColumn("KE", fKE);
          man->CreateNtupleDColumn("Edep", fEDep);
          man->CreateNtupleDColumn("x", fX);
          man->CreateNtupleDColumn("y", fY);
          man->CreateNtupleDColumn("z", fZ);
          man->CreateNtupleDColumn("lx", fLX);
          man->CreateNtupleDColumn("ly", fLY);
          man->CreateNtupleDColumn("lz", fLZ);
          man->CreateNtupleDColumn("pdx", fPdX);
          man->CreateNtupleDColumn("pdy", fPdY);
          man->CreateNtupleDColumn("pdz", fPdZ);
          man->CreateNtupleDColumn("t", fT);
          man->CreateNtupleIColumn("volID", fVolID);
          man->CreateNtupleIColumn("iRep", fIRep);
        }
        else if(fOption == kStepWise) {
          man->CreateNtupleIColumn("pid");
          man->CreateNtupleIColumn("trackID");
          man->CreateNtupleIColumn("parentID");
          man->CreateNtupleIColumn("step");
          man->CreateNtupleDColumn("KE");
          man->CreateNtupleDColumn("Edep");
          man->CreateNtupleDColumn("x");
          man->CreateNtupleDColumn("y");
          man->CreateNtupleDColumn("z");
          man->CreateNtupleDColumn("lx");
          man->CreateNtupleDColumn("ly");
          man->CreateNtupleDColumn("lz");
          man->CreateNtupleDColumn("pdx");
          man->CreateNtupleDColumn("pdy");
          man->CreateNtupleDColumn("pdz");
          man->CreateNtupleDColumn("t");
          man->CreateNtupleIColumn("volID");
          man->CreateNtupleIColumn("iRep");
        }
        else {
          cout << "ERROR: Unknown output option " << fOption << endl;
          return;
        }
        man->FinishNtuple();

        // look for filename set by macro command: /analysis/setFileName [name]
	if(man->GetFileName() == "") man->SetFileName("g4simpleout");
        cout << "Opening file " << man->GetFileName() << endl;
        man->OpenFile();

        ResetVars();
        fNEvents = G4RunManager::GetRunManager()->GetCurrentRun()->GetNumberOfEventToBeProcessed();
        fVolIDMap.clear();
      }

      fEventNumber = G4EventManager::GetEventManager()->GetConstCurrentEvent()->GetEventID();
      static G4int lastEventID = fEventNumber;
      if(fEventNumber != lastEventID) {
        if(fOption == kEventWise && fPID.size()>0) WriteRow(man);
        ResetVars();
        lastEventID = fEventNumber;
      }
/* moved forward
      // post-step point will always work: only need to use the pre-step point
      // on the first step, for which the pre-step volume is always the same as
      // the post-step volume
      G4VPhysicalVolume* vpv = step->GetPostStepPoint()->GetPhysicalVolume();
      G4int id = fVolIDMap[vpv];
      if(id == 0 && fPatternPairs.size() > 0) {
        string name = (vpv == NULL) ? "NULL" : vpv->GetName();
        for(auto& pp : fPatternPairs) {
          if(name == pp.first) {
            string replaced = pp.second;
	    cout << "Setting ID for " << name << " to " << replaced << endl;
            int id_new = stoi(replaced);
            if (id_new == 0 || id_new == -1) {
              cout << "Volume " << name << ": Can't use ID = " << id_new << endl;
            }
            else {
              id = id_new;
            }
            break;
          }
        }
        if(id == 0 && !fRecordAllSteps) id = -1;
        fVolIDMap[vpv] = id;
      }
*/

      // always record primary event info from pre-step of first step
      // if recording all steps, do this block to record prestep info
      if(fVolID.size() == 0 || (fRecordAllSteps && step->GetTrack()->GetCurrentStepNumber() == 1)) {
        fVolID.push_back(id == -1 ? 0 : id);
        fPID.push_back(step->GetTrack()->GetParticleDefinition()->GetPDGEncoding());
        fTrackID.push_back(step->GetTrack()->GetTrackID());
        fParentID.push_back(step->GetTrack()->GetParentID());
        fStepNumber.push_back(0); // call this step "0"
        fKE.push_back(step->GetPreStepPoint()->GetKineticEnergy());
        fEDep.push_back(0);
        G4ThreeVector pos = step->GetPreStepPoint()->GetPosition();
        fX.push_back(pos.x());
        fY.push_back(pos.y());
        fZ.push_back(pos.z());
        G4TouchableHandle vol = step->GetPreStepPoint()->GetTouchableHandle();
        G4ThreeVector lPos = vol->GetHistory()->GetTopTransform().TransformPoint(pos);
        fLX.push_back(lPos.x());
        fLY.push_back(lPos.y());
        fLZ.push_back(lPos.z());
        G4ThreeVector momDir = step->GetPreStepPoint()->GetMomentumDirection();
        fPdX.push_back(momDir.x());
        fPdY.push_back(momDir.y());
        fPdZ.push_back(momDir.z());
        fT.push_back(step->GetPreStepPoint()->GetGlobalTime());
        fIRep.push_back(vol->GetReplicaNumber());

        if(fOption == kStepWise) WriteRow(man);
      }

      // If not in a sensitive volume, get out of here.
      if(id == -1) return;

      // Don't write Edep=0 steps (unless desired)
      if(!fRecordAllSteps && step->GetTotalEnergyDeposit() == 0) return;

      // Now record post-step info
      fVolID.push_back(id);
      fPID.push_back(step->GetTrack()->GetParticleDefinition()->GetPDGEncoding());
      fTrackID.push_back(step->GetTrack()->GetTrackID());
      fParentID.push_back(step->GetTrack()->GetParentID());
      fStepNumber.push_back(step->GetTrack()->GetCurrentStepNumber());
      fKE.push_back(step->GetTrack()->GetKineticEnergy());
      fEDep.push_back(step->GetTotalEnergyDeposit());
      G4ThreeVector pos = step->GetPostStepPoint()->GetPosition();
      fX.push_back(pos.x());
      fY.push_back(pos.y());
      fZ.push_back(pos.z());
      G4TouchableHandle vol = step->GetPostStepPoint()->GetTouchableHandle();
      G4ThreeVector lPos = vol->GetHistory()->GetTopTransform().TransformPoint(pos);
      fLX.push_back(lPos.x());
      fLY.push_back(lPos.y());
      fLZ.push_back(lPos.z());
      G4ThreeVector momDir = step->GetPostStepPoint()->GetMomentumDirection();
      fPdX.push_back(momDir.x());
      fPdY.push_back(momDir.y());
      fPdZ.push_back(momDir.z());
      fT.push_back(step->GetPostStepPoint()->GetGlobalTime());
      fIRep.push_back(vol->GetReplicaNumber());

      if(fOption == kStepWise) WriteRow(man);
    }

    //Based on bachelor thesis Patrick Krause
    G4double fiberAtt(const G4Step *step){

	const G4double I1 = 0.042; //trapped in core prob
	const G4double I2 = 0.068; //trapped in core + first cladding prob
	const G4double I3 = 0.209; //trapped in core + first cladding + second cladding prob
	const G4double attL =3900*mm;
	const G4double attS =225*mm;

	G4Tubs* shroud = dynamic_cast<G4Tubs*>( step->GetPostStepPoint()->GetPhysicalVolume()->GetLogicalVolume()->GetSolid());
	G4double fiberLengthHalf = shroud->GetZHalfLength();
	G4ThreeVector trans = step->GetPostStepPoint()->GetPhysicalVolume()->GetTranslation();

	G4double curZdir = step->GetPostStepPoint()->GetMomentumDirection().z();
	G4double curZ = step->GetPostStepPoint()->GetPosition().z() + fiberLengthHalf - trans.z();
	G4double x=curZ;

	if(G4UniformRand()<=0.5)
		x= 2.*fiberLengthHalf-curZ;
	G4double intensity =I2*exp(-x/attL) + (I3 - I2)*exp(-x/attS);

	if(verbosity>3){
		G4cout << "Investigate the photon at z+fiberLengthHalf: " << curZ << G4endl;
		G4cout << "We calculate the travel length in the fiber to be: "<< x << G4endl;
	}

	return intensity;

    }

};		//END of Stepping Action Class Definition/Declaration


class G4SimplePrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction, public G4UImessenger
{
  public:
    G4SimplePrimaryGeneratorAction(){
      gen = new L200ParticleGenerator;

      fToggleGeneratorCmd = new G4UIcmdWithABool("/g4simple/toggleL200Gen", this);
      fToggleGeneratorCmd->SetDefaultValue(true);
      fToggleGeneratorCmd->SetGuidance("Set if the L200ParticleGenerator is used (if so, no beamOn command, and g4simple is started with two options only)");
      useGen = true;
    }
    ~G4SimplePrimaryGeneratorAction(){
      delete gen;
      delete fToggleGeneratorCmd;
    }
    void GeneratePrimaries(G4Event* event) {
	if(useGen)gen->GeneratePrimaryVertex(event);
	else fParticleGun.GeneratePrimaryVertex(event);
    }
    L200ParticleGenerator* getGenerator() {
	return gen;
    };
   void setNewValue(G4UIcommand *command, G4String newValues) {


     if(command == fToggleGeneratorCmd) {
        useGen = fToggleGeneratorCmd->GetNewBoolValue(newValues);
     }
  };
  private:
    G4GeneralParticleSource fParticleGun;
    L200ParticleGenerator* gen;
    G4UIcmdWithABool* fToggleGeneratorCmd;
    G4bool useGen;
};


class G4SimpleDetectorConstruction : public G4VUserDetectorConstruction
{
  public:
    G4SimpleDetectorConstruction(G4VPhysicalVolume *world = 0) { fWorld = world; }
    virtual G4VPhysicalVolume* Construct() { return fWorld; }
  private:
    G4VPhysicalVolume *fWorld;
};


class G4SimpleRunManager : public G4RunManager, public G4UImessenger
{
  private:
    G4UIdirectory* fDirectory;
    G4UIcmdWithAString* fPhysListCmd;
    G4UIcommand* fDetectorCmd;
    G4UIcommand* fTGDetectorCmd;
    G4UIcmdWithABool* fRandomSeedCmd;
    G4UIcmdWithAString* fListVolsCmd;
    G4UIcmdWithADouble* fSetFiberDetProbCmd;
	RunList* runList;

  public:
    G4SimpleRunManager()
	: runList(NULL)
	{
      fDirectory = new G4UIdirectory("/g4simple/");
      fDirectory->SetGuidance("Parameters for g4simple MC");

      fPhysListCmd = new G4UIcmdWithAString("/g4simple/setReferencePhysList", this);
      fPhysListCmd->SetGuidance("Set reference physics list to be used");

      fDetectorCmd = new G4UIcommand("/g4simple/setDetectorGDML", this);
      fDetectorCmd->SetParameter(new G4UIparameter("filename", 's', false));
      G4UIparameter* validatePar = new G4UIparameter("validate", 'b', true);
      validatePar->SetDefaultValue("true");
      fDetectorCmd->SetParameter(validatePar);
      fDetectorCmd->SetGuidance("Provide GDML filename specifying the detector construction");

      fTGDetectorCmd = new G4UIcommand("/g4simple/setDetectorTGFile", this);
      fTGDetectorCmd->SetParameter(new G4UIparameter("filename", 's', false));
      fTGDetectorCmd->SetGuidance("Provide text filename specifying the detector construction");

      fRandomSeedCmd = new G4UIcmdWithABool("/g4simple/setRandomSeed", this);
      fRandomSeedCmd->SetParameterName("useURandom", true);
      fRandomSeedCmd->SetDefaultValue(false);
      fRandomSeedCmd->SetGuidance("Seed random number generator with a read from /dev/random");
      fRandomSeedCmd->SetGuidance("Set useURandom to true to read instead from /dev/urandom (faster but less random)");

      fListVolsCmd = new G4UIcmdWithAString("/g4simple/listPhysVols", this);
      fListVolsCmd->SetParameterName("pattern", true);
      fListVolsCmd->SetGuidance("List name of all instantiated physical volumes");
      fListVolsCmd->SetGuidance("Optionally supply a regex pattern to only list matching volume names");
      fListVolsCmd->AvailableForStates(G4State_Idle, G4State_GeomClosed, G4State_EventProc);

      fSetFiberDetProbCmd = new G4UIcmdWithADouble("/optics/fiberDetProb", this);
      fSetFiberDetProbCmd->SetDefaultValue(0.6);
      fSetFiberDetProbCmd->SetGuidance("Set the detection probability of the fiber shrouds (coverage)!");
      fiberDetProb = 0.;

}

    ~G4SimpleRunManager() {
      delete fDirectory;
      delete fPhysListCmd;
      delete fDetectorCmd;
      delete fTGDetectorCmd;
      delete fRandomSeedCmd;
      delete fListVolsCmd;
      delete fSetFiberDetProbCmd;

		delete runList;	//have to do this to finalize written file
    }

    void SetNewValue(G4UIcommand *command, G4String newValues) {

      if(command == fSetFiberDetProbCmd){
	fiberDetProb = fSetFiberDetProbCmd->GetNewDoubleValue(newValues);
      }
      else if(command == fPhysListCmd) {
		G4VModularPhysicsList* gvmpl = (new G4PhysListFactory)->GetReferencePhysList(newValues);
		//now let's manually patch in optical physics!
		G4OpticalPhysics* opticalPhysics = new G4OpticalPhysics();
  		gvmpl->RegisterPhysics( opticalPhysics );		//it's public: it's allowed
  		opticalPhysics->SetWLSTimeProfile("delta");
		opticalPhysics->Configure(kBoundary,false);
  		opticalPhysics->SetScintillationYieldFactor(1.0);
 		opticalPhysics->SetScintillationExcitationRatio(0.0);
  		opticalPhysics->SetMaxNumPhotonsPerStep(100);
  		opticalPhysics->SetMaxBetaChangePerStep(10.0);
  		opticalPhysics->SetTrackSecondariesFirst(kCerenkov,true);
  		opticalPhysics->SetTrackSecondariesFirst(kScintillation,true);
		opticalPhysics->SetTrackSecondariesFirst(kAbsorption,true);
		opticalPhysics->SetTrackSecondariesFirst(kWLS,true);

		L200FiberPhysics* fp = new L200FiberPhysics();
		fp->setMagicMaterialName("LiquidArgonFiber");
		fp->setLArWL(128*nm);
		fp->setFiberHitProb(fiberDetProb);
		gvmpl->RegisterPhysics(fp);

        SetUserInitialization(gvmpl);
		G4SimplePrimaryGeneratorAction* gspga = new G4SimplePrimaryGeneratorAction();
        SetUserAction(gspga); // must come after phys list
		MapRunAction* mra = new MapRunAction(1);	//TODO: how many volumes?
		SetUserAction(mra);
        SetUserAction(new G4SimpleSteppingAction(mra)); // must come after phys list

		runList = new RunList(gspga->getGenerator(), mra);//last but not least
      }
      else if(command == fDetectorCmd) {
        istringstream iss(newValues);
        string filename;
        string validate;
        iss >> filename >> validate;
		if(filename == "OP_NOVICE"){
			SetUserInitialization(new OpNoviceDetectorConstruction());
		}
		else if(filename == "L200"){
			SetUserInitialization(new L200DetectorConstruction());
		}
		else{
		    G4GDMLParser parser;
		    parser.Read(filename, validate == "1" || validate == "true" || validate == "True");
		    SetUserInitialization(new G4SimpleDetectorConstruction(parser.GetWorldVolume()));
		}
      }
      else if(command == fTGDetectorCmd) {
        new G4tgrMessenger;
        G4tgbVolumeMgr* volmgr = G4tgbVolumeMgr::GetInstance();
        volmgr->AddTextFile(newValues);
        SetUserInitialization(new G4SimpleDetectorConstruction(volmgr->ReadAndConstructDetector()));
      }
      else if(command == fRandomSeedCmd) {
        bool useURandom = fRandomSeedCmd->GetNewBoolValue(newValues);
        string path = useURandom ?  "/dev/urandom" : "/dev/random";

        ifstream devrandom(path.c_str());
        if (!devrandom.good()) {
          cout << "setRandomSeed: couldn't open " << path << ". Your seed is not set." << endl;
          return;
        }

        long seed;
        devrandom.read((char*)(&seed), sizeof(long));

        // Negative seeds give nasty sequences for some engines. For example,
        // CLHEP's JamesRandom.cc contains a specific check for this. Might
        // as well make all seeds positive; randomness is not affected (one
        // bit of randomness goes unused).
        if (seed < 0) seed = -seed;

        CLHEP::HepRandom::setTheSeed(seed);
        cout << "CLHEP::HepRandom seed set to: " << seed << endl;
        devrandom.close();
      }
      else if(command == fListVolsCmd) {
        regex pattern(newValues);
        bool doMatching = (newValues != "");
        G4PhysicalVolumeStore* volumeStore = G4PhysicalVolumeStore::GetInstance();
        cout << "Physical volumes";
        if(doMatching) cout << " matching pattern " << newValues;
        cout << ":" << endl;
        for(size_t i=0; i<volumeStore->size(); i++) {
          string name = volumeStore->at(i)->GetName();
	  int iRep = volumeStore->at(i)->GetCopyNo();
          if(!doMatching || regex_match(name, pattern)) cout << name << ' ' << iRep << endl;
        }
      }
    }

	void autorun(){
		if(runList == NULL){
			G4Exception("G4SimpleRunManager::autorun","noRunList",RunMustBeAborted,"no runList here. Did you make some bad stuff in your macro?");
		}else{
			runList->startRuns();
		}
	};
};



int main(int argc, char** argv)
{
  if(argc > 3) {
    cout << "Usage: " << argv[0] << " [macro]" << "[--vis]" << endl;
    return 1;
  }




  G4SimpleRunManager* runManager = new G4SimpleRunManager;
  G4VisManager* visManager = new G4VisExecutive;
  visManager->Initialize();

  if(argc == 1) (new G4UIterminal(new G4UItcsh))->SessionStart();
  else G4UImanager::GetUIpointer()->ApplyCommand(G4String("/control/execute ")+argv[1]);

  if(argc == 2)
	runManager->autorun();

#ifdef GDMLOUT
	//only for test. Will have to find better position to vomit out gdml only on request
	G4GDMLParser parser;
    parser.Write("L200.gdml",
        G4TransportationManager::GetTransportationManager()->
        GetNavigatorForTracking()->GetWorldVolume()->GetLogicalVolume());
#endif //GDMLOUT

  delete visManager;
  delete runManager;
  return 0;
}

