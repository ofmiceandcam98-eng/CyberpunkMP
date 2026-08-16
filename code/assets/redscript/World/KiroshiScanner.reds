// KiroshiScanner.reds - Redscript integration for Kiroshi Optical Scanner RP System
// Supports Civilian View vs Tactical & Bounty Scanner View (Mercenary, Solo, Netrunner, Tech, Lawman, Fixer, Exec)

public class KiroshiScanResultData extends IScriptable {
    public let targetName: String;
    public let lifepath: String;
    public let occupation: String;
    public let affiliation: String;
    public let bio: String;

    // Tactical & Bounty Fields
    public let hasTacticalAccess: Bool;
    public let hasActiveWarrant: Bool;
    public let warrantDetails: String;
    public let criminalRecordCount: Int32;
    public let bountyAmount: Float;
}